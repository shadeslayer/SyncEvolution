#!/usr/bin/python


"""
The general idea is that tests to run are defined as a list of
actions. Each action has a unique name and can depend on other
actions to have run successfully before.

Most work is executed in directories defined and owned by these
actions. The framework only manages one directory which represents
the result of each action:
- an overview file which lists the result of each action
- for each action a directory with stderr/out and additional files
  that the action can put there
"""

import os, sys, popen2, traceback, re, time, smtplib, optparse, stat

try:
    import gzip
    havegzip = True
except:
    havegzip = False

def cd(path):
    """Enter directories, creating them if necessary."""
    if not os.access(path, os.F_OK):
        os.makedirs(path)
    os.chdir(path)

def abspath(path):
    """Absolute path after expanding vars and user."""
    return os.path.abspath(os.path.expanduser(os.path.expandvars(path)))

def del_dir(path):
    if not os.access(path, os.F_OK):
        return
    for file in os.listdir(path):
        file_or_dir = os.path.join(path,file)
        if os.path.isdir(file_or_dir) and not os.path.islink(file_or_dir):
            del_dir(file_or_dir) #it's a directory reucursive call to function again
        else:
            os.remove(file_or_dir) #it's a file, delete it
    os.rmdir(path)


def copyLog(filename, dirname, htaccess, lineFilter=None):
    """Make a gzipped copy (if possible) with the original time stamps and find the most severe problem in it.
    That line is then added as description in a .htaccess AddDescription.
    """
    info = os.stat(filename)
    outname = os.path.join(dirname, os.path.basename(filename))
    if True:
        outname = outname + ".gz"
        out = gzip.open(outname, "wb")
    else:
        out = file(outname, "w")
    error = None
    for line in file(filename, "r").readlines():
        if not error and line.find("ERROR") >= 0:
            error = line
        if lineFilter:
            line = lineFilter(line)
        out.write(line)
    out.close()
    os.utime(outname, (info[stat.ST_ATIME], info[stat.ST_MTIME]))
    if error:
        htaccess.write("AddDescription \"%s\" %s\n" %
                       (error.strip().replace("\"", "'").replace("<", "&lt;").replace(">","&gt;"),
                        os.path.basename(filename)))

class Action:
    """Base class for all actions to be performed."""

    DONE = "0 DONE"
    WARNINGS = "1 WARNINGS"
    FAILED = "2 FAILED"
    TODO = "3 TODO"
    SKIPPED = "4 SKIPPED"
    COMPLETED = (DONE, WARNINGS)

    def __init__(self, name):
        self.name = name
        self.status = self.TODO
        self.summary = ""
        self.dependencies = []

    def execute(self):
        """Runs action. Throws an exeception if anything fails.
        Will be called by tryexecution() with stderr/stdout redirected into a file
        and the current directory set to an empty temporary directory.
        """
        raise Exception("not implemented")

    def tryexecution(self, step, logs):
        """wrapper around execute which handles exceptions, directories and stdout"""
        if logs:
            fd = -1
            oldstdout = os.dup(1)
            oldstderr = os.dup(2)
            oldout = sys.stdout
            olderr = sys.stderr
        cwd = os.getcwd()
        try:
            subdirname = "%d-%s" % (step, self.name)
            del_dir(subdirname)
            sys.stderr.flush()
            sys.stdout.flush()
            cd(subdirname)
            if logs:
                fd = os.open("output.txt", os.O_WRONLY|os.O_CREAT|os.O_TRUNC)
                os.dup2(fd, 1)
                os.dup2(fd, 2)
                sys.stdout = os.fdopen(fd, "w")
                sys.stderr = sys.stdout
            print "=== starting %s ===" % (self.name)
            self.execute()
            self.status = Action.DONE
            self.summary = "okay"
        except Exception, inst:
            traceback.print_exc()
            self.status = Action.FAILED
            self.summary = str(inst)

        print "\n=== %s: %s ===" % (self.name, self.status)
        sys.stdout.flush()
        os.chdir(cwd)
        if logs:
            if fd >= 0:
                os.close(fd)
                os.dup2(oldstdout, 1)
                os.dup2(oldstderr, 2)
                sys.stderr = olderr
                sys.stdout = oldout
                os.close(oldstdout)
                os.close(oldstderr)
        return self.status

class Context:
    """Provides services required by actions and handles running them."""

    def __init__(self, tmpdir, resultdir, workdir, mailtitle, sender, recipients, enabled, skip, nologs):
        # preserve normal stdout because stdout/stderr will be redirected
        self.out = os.fdopen(os.dup(1), "w")
        self.todo = []
        self.actions = {}
        self.tmpdir = abspath(tmpdir)
        self.resultdir = abspath(resultdir)
        self.workdir = abspath(workdir)
        self.summary = []
        self.mailtitle = mailtitle
        self.sender = sender
        self.recipients = recipients
        self.enabled = enabled
        self.skip = skip
        self.nologs = nologs

    def runCommand(self, cmd):
        """Log and run the given command, throwing an exception if it fails."""
        print "%s: %s" % (os.getcwd(), cmd)
        sys.stdout.flush()
        result = os.system(cmd)
        if result != 0:
            raise Exception("%s: failed (return code %d)" % (cmd, result))

    def add(self, action):
        """Add an action for later execution. Order is important, fifo..."""
        self.todo.append(action)
        self.actions[action.name] = action

    def required(self, actionname):
        """Returns true if the action is required by one which is enabled."""
        if actionname in self.enabled:
            return True
        for action in self.todo:
            if actionname in action.dependencies and self.required(action.name):
                return True
        return False

    def execute(self):
        cd(self.resultdir)
        s = file("output.txt", "w+")
        status = Action.DONE

        step = 0
        while len(self.todo) > 0:
            try:
                step = step + 1

                # get action
                action = self.todo.pop(0)

                # check whether it actually needs to be executed
                if self.enabled and \
                       not action.name in self.enabled and \
                       not self.required(action.name):
                    # disabled
                    action.status = Action.SKIPPED
                    self.summary.append("%s skipped: disabled in configuration" % (action.name))
                elif action.name in self.skip:
                    # assume that it was done earlier
                    action.status = Action.SKIPPED
                    self.summary.append("%s assumed to be done: requested by configuration" % (action.name))
                else:
                    # check dependencies
                    for depend in action.dependencies:
                        if not self.actions[depend].status in Action.COMPLETED:
                            action.status = Action.SKIPPED
                            self.summary.append("%s skipped: required %s has not been executed" % (action.name, depend))
                            break

                if action.status == Action.SKIPPED:
                    continue

                # execute it
                action.tryexecution(step, not self.nologs)
                if action.status > status:
                    status = action.status
                if action.status == Action.FAILED:
                    self.summary.append("%s: %s" % (action.name, action.summary))
                elif action.status == Action.WARNINGS:
                    self.summary.append("%s done, but check the warnings" % action.name)
                else:
                    self.summary.append("%s successful" % action.name)
            except Exception, inst:
                traceback.print_exc()
                self.summary.append("%s failed: %s" % (action.name, inst))

        # update summary
        s.write("%s\n" % ("\n".join(self.summary)))
        s.close()

        # report result by email
        if self.recipients:
            server = smtplib.SMTP("localhost")
            msg = "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s" % \
                  (self.sender,
                   ", ".join(self.recipients),
                   self.mailtitle,
                   "\n".join(self.summary))
            failed = server.sendmail(self.sender, self.recipients, msg)
            if failed:
                print "could not send to: %s" % (failed)
                sys.exit(1)
        else:
            print "\n".join(self.summary), "\n"

        if status in Action.COMPLETED:
            sys.exit(0)
        else:
            sys.exit(1)

# must be set before instantiating some of the following classes
context = None
        
class CVSCheckout(Action):
    """Does a CVS checkout (if directory does not exist yet) or an update (if it does)."""
    
    def __init__(self, name, workdir, runner, cvsroot, module, revision):
        """workdir defines the directory to do the checkout in,
        cvsroot the server, module the path to the files,
        revision the tag to checkout"""
        Action.__init__(self,name)
        self.workdir = workdir
        self.runner = runner
        self.cvsroot = cvsroot
        self.module = module
        self.revision = revision
        self.basedir = os.path.join(abspath(workdir), module)

    def execute(self):
        cd(self.workdir)
        if os.access(self.module, os.F_OK):
            os.chdir(self.module)
            context.runCommand("cvs update -d -r %s"  % (self.revision))
        elif self.revision == "HEAD":
            context.runCommand("cvs -d %s checkout %s" % (self.cvsroot, self.module))
            os.chdir(self.module)
        else:
            context.runCommand("cvs -d %s checkout -r %s %s" % (self.cvsroot, self.revision, self.module))
            os.chdir(self.module)
        if os.access("autogen.sh", os.F_OK):
            context.runCommand("%s ./autogen.sh" % (self.runner))

class ClientCheckout(CVSCheckout):
    def __init__(self, name, revision):
        """checkout C++ client source code and apply all patches"""
        CVSCheckout.__init__(self,
                             name, context.workdir, options.shell,
                             ":ext:pohly@cvs.forge.objectweb.org:/cvsroot/sync4j",
                             "3x/client-api/native",
                             revision)

    def execute(self):
        # undo patches before upgrading
        try:
            os.chdir(self.basedir)
            context.runCommand("patcher -B")
        except:
            pass
        # checkout/upgrade
        CVSCheckout.execute(self)
        #patch again
        context.runCommand("patcher -A")


class AutotoolsBuild(Action):
    def __init__(self, name, src, configargs, runner, dependencies):
        """Runs configure from the src directory with the given arguments.
        runner is a prefix for the configure command and can be used to setup the
        environment."""
        Action.__init__(self, name)
        self.src = src
        self.configargs = configargs
        self.runner = runner
        self.dependencies = dependencies
        self.installdir = os.path.join(context.tmpdir, "install")
        self.builddir = os.path.join(context.tmpdir, "build")

    def execute(self):
        del_dir(self.builddir)
        cd(self.builddir)
        context.runCommand("%s %s/configure --prefix=%s %s" % (self.runner, self.src, self.installdir, self.configargs))
        context.runCommand("%s make install" % (self.runner))


class SyncEvolutionTest(Action):
    def __init__(self, name, build, serverlogs, runner, tests, testenv, lineFilter=None):
        """Execute TestEvolution for all (empty tests) or the
        selected tests."""
        Action.__init__(self, name)
        self.srcdir = os.path.join(build.builddir, "src")
        self.serverlogs = serverlogs
        self.runner = runner
        self.tests = tests
        self.testenv = testenv
        self.dependencies.append(build.name)
        self.lineFilter = lineFilter

    def execute(self):
        resdir = os.getcwd()
        os.chdir(self.srcdir)
        try:
            basecmd = "%s TEST_EVOLUTION_ALARM=600 TEST_EVOLUTION_LOG=%s %s ./TestEvolution" % (self.testenv, self.serverlogs, self.runner);
            context.runCommand("make testclean test")
            if self.tests:
                ex = None
                for test in self.tests:
                    try:
                        context.runCommand("%s %s" % (basecmd, test))
                    except Exception, inst:
                        if not ex:
                            ex = inst
                if ex:
                    raise ex
            else:
                context.runCommand(basecmd)
        finally:
            tocopy = re.compile(r'.*\.log')
            htaccess = file(os.path.join(resdir, ".htaccess"), "a")
            for f in os.listdir(self.srcdir):
                if tocopy.match(f):
                    copyLog(f, resdir, htaccess, self.lineFilter)



###################################################################
# Configuration part
###################################################################

parser = optparse.OptionParser()
parser.add_option("-e", "--enable",
                  action="append", type="string", dest="enabled",
                  help="use this to enable specific actions instead of executing all of them (can be used multiple times)")
parser.add_option("-n", "--no-logs",
                  action="store_true", dest="nologs",
                  help="print to stdout/stderr directly instead of redirecting into log files")
parser.add_option("-l", "--list",
                  action="store_true", dest="list",
                  help="list all available actions")
parser.add_option("-s", "--skip",
                  action="append", type="string", dest="skip", default=[],
                  help="instead of executing this action assume that it completed earlier (can be used multiple times)")
parser.add_option("", "--tmp",
                  type="string", dest="tmpdir", default="",
                  help="temporary directory for intermediate files")
parser.add_option("", "--workdir",
                  type="string", dest="workdir", default="",
                  help="directory for files which might be reused between runs")
parser.add_option("", "--resultdir",
                  type="string", dest="resultdir", default="",
                  help="directory for log files and results")
parser.add_option("", "--shell",
                  type="string", dest="shell", default="",
                  help="a prefix which is put in front of a command to execute it (can be used for e.g. run_garnome)")
parser.add_option("", "--syncevo-tag",
                  type="string", dest="syncevotag", default="HEAD",
                  help="the tag of SyncEvolution (default HEAD)")
parser.add_option("", "--client-tag",
                  type="string", dest="clienttag", default="HEAD",
                  help="the tag of the client library (default HEAD)")
parser.add_option("", "--bin-suffix",
                  type="string", dest="binsuffix", default="",
                  help="string to append to binary distribution archive (default empty = no binary distribution built)")
parser.add_option("", "--synthesis",
                  type="string", dest="synthesisdir", default="",
                  help="directory with Synthesis installation")
parser.add_option("", "--funambol",
                  type="string", dest="funamboldir", default="/scratch/Funambol",
                  help="directory with Funambol installation")
parser.add_option("", "--from",
                  type="string", dest="sender",
                  help="sender of email if recipients are also specified")
parser.add_option("", "--to",
                  action="append", type="string", dest="recipients",
                  help="recipient of result email (option can be given multiple times)")
parser.add_option("", "--subject",
                  type="string", dest="subject", default="SyncML Tests " + time.strftime("%Y-%m-%d"),
                  help="subject of result email (default is \"SyncML Tests <date>\"")

(options, args) = parser.parse_args()
if options.recipients and not options.sender:
    print "sending email also requires sender argument"
    sys.exit(1)

context = Context(options.tmpdir, options.resultdir, options.workdir,
                  options.subject, options.sender, options.recipients,
                  options.enabled, options.skip, options.nologs)

class SyncEvolutionCheckout(CVSCheckout):
    def __init__(self, name, revision):
        """checkout SyncEvolution"""
        CVSCheckout.__init__(self,
                             name, context.workdir, options.shell,
                             ":ext:pohly@sync4jevolution.cvs.sourceforge.net:/cvsroot/sync4jevolution",
                             "sync4jevolution",
                             revision)

class SyncEvolutionBuild(AutotoolsBuild):
    def execute(self):
        AutotoolsBuild.execute(self)
        os.chdir("src")
        context.runCommand("%s make test" % (self.runner))

client = ClientCheckout("client-api", options.clienttag)
context.add(client)
sync = SyncEvolutionCheckout("syncevolution", options.syncevotag)
context.add(sync)
compile = SyncEvolutionBuild("compile",
                             sync.basedir,
                             "--disable-shared CXXFLAGS=-g --with-sync4j-src=%s" % (client.basedir),
                             options.shell,
                             [ client.name, sync.name ])
context.add(compile)

class SyncEvolutionDist(AutotoolsBuild):
    def __init__(self, name, binsuffix, binrunner, dependencies):
        """Builds a normal and a binary distribution archive in a directory where
        SyncEvolution was configured and compiled before.
        """
        AutotoolsBuild.__init__(self, name, "", "", binrunner, dependencies)
        self.binsuffix = binsuffix
        
    def execute(self):
        cd(self.builddir)
        if self.binsuffix:
            context.runCommand("%s make BINSUFFIX=%s distbin distcheck" % (self.runner, self.binsuffix))
        else:
            context.runCommand("%s make distcheck" % (self.runner))

dist = SyncEvolutionDist("dist",
                         options.binsuffix,
                         options.shell,
                         [ compile.name ])
context.add(dist)

evolutiontest = SyncEvolutionTest("evolution", compile,
                                  "", options.shell,
                                  [ "ContactSource", "CalendarSource", "TaskSource" ],
                                  "")
context.add(evolutiontest)

scheduleworldtest = SyncEvolutionTest("scheduleworld", compile,
                                      "", options.shell,
                                      # [ "ContactSync", "ContactStress", "TaskSync", "TaskStress", "CalendarSync", "CalendarStress" ]
                                      [ ],
                                      # ContactSync::testItems - temporary problem with tabs
                                      # CalendarSync::testItems, CalendarSync::testTwinning - temporary problem with lost timezone
                                      "TEST_EVOLUTION_SERVER=scheduleworld TEST_EVOLUTION_DELAY=2 TEST_EVOLUTION_FAILURES=ContactSync::testItems,ContactSync::testTwinning,CalendarSync::testDeleteAllRefresh,CalendarSync::testItems,TaskSync::testDeleteAllRefresh,TaskSync::testItems,TaskSync::testTwinning,ContactSync::testItems,CalendarSync::testItems,CalendarSync::testTwinning")
context.add(scheduleworldtest)

egroupwaretest = SyncEvolutionTest("egroupware", compile,
                                   "", options.shell,
                                   [ "ContactSync", "CalendarSync::testCopy", "CalendarSync::testUpdate", "CalendarSync::testDelete" ],
                                   # ContactSync::testRefreshFromServerSync,ContactSync::testRefreshFromClientSync,ContactSync::testDeleteAllRefresh,ContactSync::testRefreshSemantic,ContactSync::testRefreshStatus - refresh-from-client not supported by server
                                   # ContactSync::testOneWayFromClient - not supported by server?
                                   # ContactSync::testItems - loses a lot of information
                                   # ContactSync::testComplexUpdate - only one phone number preserved
                                   # ContactSync::testMaxMsg,ContactSync::testLargeObject,ContactSync::testLargeObjectBin - server fails to parse extra info?
                                   # ContactSync::testTwinning - duplicates contacts
                                   # CalendarSync::testCopy,CalendarSync::testUpdate - shifts time?
                                   "TEST_EVOLUTION_SERVER=egroupware TEST_EVOLUTION_FAILURES=ContactSync::testRefreshFromServerSync,ContactSync::testRefreshFromClientSync,ContactSync::testDeleteAllRefresh,ContactSync::testRefreshSemantic,ContactSync::testRefreshStatus,ContactSync::testOneWayFromClient,ContactSync::testAddUpdate,ContactSync::testItems,ContactSync::testComplexUpdate,ContactSync::testTwinning,ContactSync::testMaxMsg,ContactSync::testLargeObject,ContactSync::testLargeObjectBin,CalendarSync::testCopy,CalendarSync::testUpdate",
                                   lambda x: x.replace('oasis.ethz.ch','<host hidden>').\
                                             replace('cG9obHk6cWQyYTVtZ1gzZk5GQQ==','xxx'))
context.add(egroupwaretest)

class SynthesisTest(SyncEvolutionTest):
    def __init__(self, name, build, synthesisdir, runner):
        SyncEvolutionTest.__init__(self, name, build, os.path.join(synthesisdir, "logs"),
                                   runner, [ "ContactSync", "ContactStress" ], "TEST_EVOLUTION_SERVER=synthesis")
        self.synthesisdir = synthesisdir
        # self.dependencies.append(evolutiontest.name)

    def execute(self):
        context.runCommand("synthesis start \"%s\"" % (self.synthesisdir))
        time.sleep(5)
        try:
            SyncEvolutionTest.execute(self)
        finally:
            context.runCommand("synthesis stop \"%s\"" % (self.synthesisdir))

synthesis = SynthesisTest("synthesis", compile,
                          options.synthesisdir,
                          options.shell)
context.add(synthesis)

class FunambolTest(SyncEvolutionTest):
    def __init__(self, name, build, funamboldir, runner):
        SyncEvolutionTest.__init__(self, name, build, os.path.join(funamboldir, "ds-server", "logs", "funambol_ds.log"),
                                   runner, [ "ContactSync", "ContactStress" ],
                                   "TEST_EVOLUTION_DELAY=10 TEST_EVOLUTION_FAILURES= TEST_EVOLUTION_SERVER=funambol")
        self.funamboldir = funamboldir
        # self.dependencies.append(evolutiontest.name)

    def execute(self):
        if self.funamboldir:
            context.runCommand("%s/tools/bin/funambol.sh start" % (self.funamboldir))
        time.sleep(5)
        try:
            SyncEvolutionTest.execute(self)
        finally:
            if self.funamboldir:
                context.runCommand("%s/tools/bin/funambol.sh stop" % (self.funamboldir))

funambol = FunambolTest("funambol", compile,
                        options.funamboldir,
                        options.shell)
context.add(funambol)

if options.list:
    for action in context.todo:
        print action.name
else:
    context.execute()
