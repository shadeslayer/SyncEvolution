#!/usr/bin/python
#
# Copyright (C) 2010 Intel Corporation
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) version 3.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301  USA

'''
Automatically trying different configurations for a phone to sync with
SyncEvolution.
'''
import sys, optparse, os, time, tempfile
import shutil
import ConfigParser
import glob
import os.path

# source names as commonly used in SyncEvolution
allSources = ['addressbook', 'calendar', 'todo', 'memo', 'calendar+todo']
# valid SyncMLVersion values
allVersions = ['1.2', '1.1', '1.0']

########################### cmdline options ##########################################
parser = optparse.OptionParser()
parser.add_option("-b", "--bt-address", action = "store", type = "string",
        dest = "btaddr", help = "The Bluetooth mac address for the testing phone", default = "")
parser.add_option ("-p", "--protocol-version", action = "store", type = "string",
                   dest="version", help = "The SyncML protocal version for testing, can be one of " +
                   "|".join(allVersions) + ", by default it will try all versions one by one",
                   default = "")
parser.add_option ("-s", "--source", action = "store", type = "string", dest=
        "source", help = "The local database for testing, can be one of " + "|".join(allSources),
        default = "")
parser.add_option ("-u", "--uri", action = "store", type = "string", dest =
        "uri", help = "The URI for testing the selected source, invalid when no specific source is selected via --source", default = "")
parser.add_option ("-t", "--type", action = "store", type = "string", dest =
        "type", help = "The content type for testing the selected source, invalid when no specific source is selected via --source"
        ,default = "")
parser.add_option ("-i", "--identifier", action = "store", type="string",
        dest = "identifier", help = "The identifier used when contacting the phone, can be arbitray string. By default it will try 'PC Suite','Nokia PC Suite' and empty string",
        default = "")
parser.add_option ("", "--without-ctcap", action = "store_true", default =False,
        dest = "ctcap", help = "Testing without sending CTCap information")
parser.add_option ("-v", "--verbose", action = "count",
        dest = "verbose", help = "Increase amount of output")
parser.add_option ("-a", "--advanced", action = "store_true", default = False,
        dest = "advanced", help = "More extensive test with sending/receving data, WARNING: will destroy your data on the tested phone")
parser.add_option("-c", "--create-config", action ="store", type = "string", 
        dest = "create", help = "If set, a configuration file with the name will be created based on the testing result",
        default ="")
(options, args) = parser.parse_args()

####################semantic check for  cmdline options #######################################
if not options.btaddr:
    parser.error ("Please provide the Bluetooth MAC address for the phone with -b/--bt-address.")
if options.version and options.version not in allVersions:
    parser.error("Option -p/--protocol-version can only be one of " + "|".join(allVersions) + ".")
if options.source and options.source not in allSources:
    parser.error("Option -s/--source can only be one of " + "|".join(allSources) + ".")
if options.uri and not options.source:
    parser.error ("Option -u/--uri only works in combination with -s/--source.")
if options.type and not options.source:
    parser.error ("Option -t/--type only works in combination with -s/--source.")

#######################some global parameters ######################
syncevoCmd = 'syncevolution'
configName = 'test-phone'        # inside temporary testConfig dir
# real paths set in main() inside temporary directory
testFolder = '/dev/null/data'
testResult = '/dev/null/cache'
testConfig = '/dev/null/config'


#################### Configuration Parameter #######################
class ConfigurationParameter:
    def __init__ (self, version, source, uri, type, ctcap, identifier):
        self.version = version 
        self.source = source
        self.uri = uri 
        self.type = type 
        self.ctcap = ctcap 
        self.identifier = identifier

    def printMe(self):
        print "Test parameter: "
        print "With CTCap:     %s" %(self.ctcap,)
        print "Identifier:     %s" %(self.identifier,)
        print "SyncML version: %s" %(self.version,)
        print "Sync Source:    %s" %(self.source,)
        print "URI:            %s" %(self.uri,)
        print "Content Type:   %s" %(self.type,)

    def __str__(self):
        res = []
        if self.ctcap:
            res.append("with CTCap")
        else:
            res.append("without CTCap")
        res.append(self.identifier)
        res.append(self.version)
        res.append("%s = %s + %s" % (self.source, self.uri, self.type))
        return ", ".join(res)

    def equalWith(self, config):
        return (config and \
                self.ctcap == config.ctcap and \
                self.identifier == config.identifier and \
                self.version == config.version and \
                self.uri == config.uri and \
                self.type == config.type)

###################### utility functions ####################
def clearLocalSyncData(sources):
    for source in sources:
        dirname = "%s/%s" % (testFolder, source)
        rm_r(dirname)
        os.makedirs(dirname)

def createFile(filename, content):
    f = open(filename, "w")
    f.write(content)

def insertLocalSyncData(sources, type):
    for source in sources:
        testcase, keys = getTestCase (source, type)
        createFile(os.path.join(testFolder, source, "0"), testcase)

def getTestCase(source, type):
    """Returns a pair of test item string plus a list of sub strings
    which are expected to come back from the phone. Type comparison is
    intentionally a bit vague, so that it doesn't matter whether the
    type contains a version or a ! force flag."""
    if source == 'addressbook' and type.startswith('text/vcard'):
        return  ("BEGIN:VCARD\n"
                 "VERSION:3.0\n"
                 "TITLE:tester\n"
                 "FN:John Doe\n"
                 "N:Doe;John;;;\n"
                 "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
                 "X-EVOLUTION-FILE-AS:Doe\\, John\n"
                 "X-MOZILLA-HTML:FALSE\n"
                 "NOTE:test-phone\n"
                 "END:VCARD\n",
                 ["VCARD", "TITLE:tester", "Doe", "John"])

    if source == 'addressbook' and type.startswith('text/x-vcard'):
        return ("BEGIN:VCARD\n"
                "VERSION:2.1\n"
                "TITLE:tester\n"
                "FN:John Doe\n"
                "N:Doe;John;;;\n"
                "TEL;TYPE=WORK;TYPE=VOICE:business 1\n"
                "X-MOZILLA-HTML:FALSE\n"
                "NOTE:REVISION\n"
                "END:VCARD\n",
                ["VCARD", "TITLE:tester", "Doe", "John"])

    if source == 'calendar' and type.startswith('text/calendar'):
        return ("BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:phone meeting\n"
                "DTEND:20060406T163000Z\n"
                "DTSTART:20060406T160000Z\n"
                "DTSTAMP:20060406T211449Z\n"
                "LAST-MODIFIED:20060409T213201\n"
                "CREATED:20060409T213201\n"
                "LOCATION:my office\n"
                "DESCRIPTION:let's talkREVISION\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n",
                ["VCALENDAR", "VEVENT", "phone meeting", "my office"])

    if source == 'calendar' and type.startswith('text/x-vcalendar'):
        return ("BEGIN:VCALENDAR\n"
                "VERSION:1.0\n"
                "BEGIN:VEVENT\n"
                "SUMMARY:phone meeting\n"
                "DTEND:20060406T163000Z\n"
                "DTSTART:20060406T160000Z\n"
                "DTSTAMP:20060406T211449Z\n"
                "LOCATION:my office\n"
                "DESCRIPTION:let's talkREVISION\n"
                "END:VEVENT\n"
                "END:VCALENDAR\n",
               ["VCALENDAR", "VEVENT", "phone meeting", "my office"])
 
    if source == 'todo' and type.startswith('text/calendar'):
        return ("BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:2.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me\n"
                "DESCRIPTION:to be doneREVISION\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n",
                ["VCALENDAR", "VTODO", "do me"]) 

    if source == 'todo' and type.startswith('text/x-vcalendar'):
        return ("BEGIN:VCALENDAR\n"
                "PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"
                "VERSION:1.0\n"
                "METHOD:PUBLISH\n"
                "BEGIN:VTODO\n"
                "DTSTAMP:20060417T173712Z\n"
                "SUMMARY:do me\n"
                "DESCRIPTION:to be doneREVISION\n"
                "CREATED:20060417T173712\n"
                "LAST-MODIFIED:20060417T173712\n"
                "END:VTODO\n"
                "END:VCALENDAR\n",
                ["VCALENDAR", "VTODO", "do me"])

    if source == 'memo':
        return ("Summary Line\n"
                "BODY TEXT\n",
                ["Summary Line"])

    raise "no test data defined for source %s and type %s" % (source, type)

# Compare the received data with the sent data, we only match selected keywords in received
# data for a basic sanity test
def compareSyncData(sources, type):
    for source in sources:
        testcase, keys  = getTestCase (source, type)
        received = ''
        recFile = "%s/%s/0" %(testFolder, source)
        try:
            rf = open (recFile)
            received = rf.read()
        except:
            return False
        if (options.verbose > 1):
            print "comparing received file:"
            print received
            print "with built in keywords in test case:"
            print keys
        for key in keys:
            if (received.find(key) <0):
                return False
        return True

# wrapper of running a shell command
def runCommand(cmd, exception = True):
    """Log and run the given command, throwing an exception if it fails."""
    if (options.verbose > 1):
        print "%s: %s" % (os.getcwd(), cmd)
    else:
        cmd += ' >/dev/null'
    sys.stdout.flush()
    result = os.system(cmd)
    if result != 0 and exception:
        raise Exception("%s: failed (return code %d)" % (cmd, result>>8))

def runSync(sync):
    rm_r("%s/syncevolution" % testResult)
    status = True
    interrupt = False

    try:
        runCommand (sync)
    except:
        status = False
        pass

    # session name is unknown, but we know there is only one, so let
    # glob find it for us
    resultFile = glob.glob("%s/syncevolution/*/status.ini"  % testResult)[0]

    # inject [main] at start of file for ConfigParser,
    # because SyncEvolution doesn't write sections
    class IniFile:
        def __init__(self, filename):
            self.fp = open(filename, "r")
            self.read = False
        def readline(self):
            if not self.read:
                self.read = True
                return "[main]"
            else:
                return self.fp.readline()

    ini = ConfigParser.ConfigParser({"status": "0", "error": ""})
    ini.readfp(IniFile(resultFile))
    statuscode = ini.get("main", "status")
    if statuscode == "20015":
        # aborted by user, stop testing
        status = False
        interrupt = True
    if statuscode == "22002":
        # syncevolution failed (for example, kill -9), warn and abort
        print "\nSyncEvolution binary died prematurely, aborting testing."
        status = False
        interrupt = True
    return (status, interrupt)

# recursive directory removal, without throwing an error if directory does not exist
def rm_r(dirname):
    if os.path.isdir(dirname):
        shutil.rmtree(dirname)

def hash2ini(hash):
    """convert key/value pairs into .ini file without sections"""
    res = []
    for key, value in hash.items():
        res.append("%s = %s" % (key, value))
    return "\n".join(res)

def strip_version(type):
    """turn type[:version][!] into type[!]"""
    res = type.split(':')[0]
    if type.endswith('!'):
        res += '!'
    return res

##############################TestConfiguration##################################
class TestingConfiguration():
    def __init__(self, versions, sources, uris, types, ctcaps, identifiers, btaddr):
        if (versions):
            self.versions = versions
        else:
            self.versions = allVersions

        # If "calendar+todo" is tested, then "calendar" and "todo"
        # must be tested first. If they lead to the same result,
        # then they have to be combined for "calendar+todo".
        # "calendar+todo" itself is never tested directly.
        if sources:
            self.sources = sources
        else:
            self.sources = allSources
        if "calendar+todo" in self.sources:
            self.sources.remove("calendar+todo")
            if not "calendar" in self.sources:
                self.sources.append("calendar")
            if not "todo" in self.sources:
                self.sources.append("todo")

        if (uris):
            self.uris = uris
        else:
            self.uris = {}
            self.uris['addressbook'] = ['Contact', 'contact', 'Contacts', 'contacts', 'Addressbook', 'addressbook']
            self.uris['calendar'] = ['Calendar', 'calendar', 'Agenda','agenda']
            self.uris['todo'] = self.uris['calendar'] + ['Task', 'task', 'Tasks', 'tasks', 'Todo','todo']
            self.uris['memo'] = ['Memo', 'memo', 'Notes', 'notes', 'Note', 'note']

        if (types):
            self.types = types
        else:
            # - must include version numbers because file backend needs them
            # - current types like 'text/vcard:3.0' are "downgraded" to the
            #   legacy types when sending a SAN, so they are basically identical;
            #   to really send a SAN with these current types, we have to "force" them
            self.types = {}
            self.types['addressbook'] = ['text/vcard:3.0', 'text/x-vcard:2.1', 'text/vcard:3.0!']
            self.types['calendar'] = self.types['todo'] = ['text/calendar:2.0', 'text/x-vcalendar:1.0', 'text/calendar:2.0!']
            self.types['memo'] = ['text/plain:1.0']

        if (ctcaps):
            self.ctcaps = ctcaps
        else:
            self.ctcaps =  [True, False]
        if (identifiers):
            self.identifiers = identifiers
        else:
            self.identifiers = ['PC Suite','','Nokia PC Suite']
        self.btaddr = btaddr


    #before each configuration is really tested, prepare is called.
    #returns True if we decide current configuration need not be tested
    def prepare (self, allconfigs, curconfig):
        # Decide whether this config should be skipped (because we already found
        # a working configuration
        # Test is skipped either because 
        # 1) we already found a working configuration for the data source;
        # 2) based on the working configuration for source A, we can reasonably
        # guess a working configuration for source B must have the same
        # 'identifier', 'ctcap' and 'SyncMLVersion' setting.
        # 3) we already found a working configuration for combined calendar and
        # task, thus seperate testing for calendar and task is not needed.
        skip = False
        for source, config in self.wConfigs.items():
            if (config):
                if ( (config.source == self.source) or (config.identifier != self.identifier ) or (config.ctcap != self.ctcap) or (config.version != self.version)):
                    skip = True
        if (skip):
            if (options.verbose > 1):
                print "Test %d/%d skipped because already found a working configuration" % (curconfig, allconfigs)
            elif options.verbose > 0:
                print "Test %d/%d skipped" %(curconfig, allconfigs), \
                    ConfigurationParameter(self.version, self.source, self.uri, self.type, self.ctcap, self.identifier)
            else:
                print "Test %d/%d skipped" %(curconfig, allconfigs)
        else:
            print ("Start %d/%d test" % (curconfig, allconfigs)),
            if (options.verbose > 0):
                config = ConfigurationParameter(self.version, self.source, self.uri, self.type, self.ctcap, self.identifier)
                if (options.verbose > 1):
                    print
                    config.printMe()
                else:
                    print config
            else:
                print

        return skip


    #run the real sync test with current configuration parameter
    #if advanced option is set and the basic test succeed, it will contintue with 
    #the advanced test
    def testWithCurrentConfiguration(self):
        """ Prepare the configuration and run a sync session, Returns true if
        the test was successful, otherwise false"""
        rm_r(testConfig)
        cmdPrefix = "XDG_CACHE_HOME=%s XDG_CONFIG_HOME=%s " %(testResult, testConfig)
        syncevoTest = "%s %s --daemon=no" % (cmdPrefix, syncevoCmd)
        runCommand ("%s -c --template 'SyncEvolution Client' --sync-property peerIsClient=1 %s" % (syncevoTest, configName))
        # set the local database
        filesource = testFolder+'/'+self.source
        configCmd = "%s --configure --source-property evolutionsource='file:///%s' %s %s" %(syncevoTest, filesource, configName, self.source) 
        runCommand (configCmd)

        configCmd = "%s --configure --sync-property logLevel=5 --sync-property SyncURL=obex-bt://%s --sync-property SyncMLVersion=%s %s" % (syncevoTest, self.btaddr,self.version, configName)
        runCommand (configCmd)

        if (self.identifier):
            configCmd = "%s --configure --sync-property remoteIdentifier='%s' %s" %(syncevoTest, self.identifier, configName)
            runCommand (configCmd)

        configCmd = "%s --configure --source-property 'type=file:%s' --source-property uri=%s %s %s" %(syncevoTest, self.type, self.uri, configName, self.source)
        runCommand (configCmd)

        """ start the sync session """
        if (not self.ctcap):
            cmdPrefix += "SYNCEVOLUTION_NOCTCAP=t "
        cmdPrefix += "SYNCEVOLUTION_NO_SYNC_SIGNALS=1"
        syncCmd = " ".join((cmdPrefix, syncevoCmd, "--daemon=no", configName, self.source))
        (status,interrupt) = runSync(syncCmd)
        if (options.advanced and status and not interrupt):
            (status,interrupt)= self.advancedTestWithCurrentConfiguration()
        return (status, interrupt)

    '''Basic test for sending/receiving data
    It will work as:
    Clear local data and data on the phone via 'slow-sync' and 'two-way' sync
    Send local test case to the phone via 'two-way'
    Clear local data and get the data from the phone via 'slow-sync'
    compare the sent data with the received data to decide whether the test was successful

    Note that this depends on the phone support 'slow-sync' and 'two-way' sync and 
    implements the semantics correctly as specified in the spec. Otherwise the results will
    be undefined.
    '''
    def advancedTestWithCurrentConfiguration (self):
        """ 
        Sending/receving real data for basic sanity test
        """
        sources = []
        sources.append(self.source)

        #step 1: clean the data both locally and remotely using a 'slow-sync' and 'two-way'
        clearLocalSyncData(sources)
        cmdPrefix="XDG_CACHE_HOME=%s XDG_CONFIG_HOME=%s " % (testResult, testConfig)
        if (not self.ctcap):
            cmdPrefix += "SYNCEVOLUTION_NOCTCAP=t "
        syncevoTest = "%s %s --daemon=no" % (cmdPrefix, syncevoCmd)
        syncCmd = "%s --sync slow %s %s" % (syncevoTest, configName, self.source)
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)
        clearLocalSyncData(sources)
        syncCmd = "%s --sync two-way %s %s" % (syncevoTest, configName, self.source)
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)

        #step 2: insert testcase to local data and sync with 'two-way'
        insertLocalSyncData(sources, self.type)
        syncCmd = "%s --sync two-way %s %s" % (syncevoTest, configName, self.source)
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)

        #step 3: delete local data and sync with 'slow-sync'
        clearLocalSyncData(sources)
        syncCmd = "%s --sync slow %s %s" % (syncevoTest, configName, self.source)
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)

        #step 4: compare the received data with test case
        status = compareSyncData(sources, self.type)
        return (status, interrupt)

    '''
    The test driver iterating all possible test combinations and try them one by one
    '''
    def run(self):
        #first round of iterating, calculating all possible configuration numbers
        allconfigs = 0
        for self.ctcap in self.ctcaps:
            for self.identifier in self.identifiers:
                for self.version in self.versions:
                    for self.source in self.sources:
                        for self.uri in self.uris[self.source]:
                            for self.type in self.types[self.source]:
                                allconfigs +=1
        print "Starting test for %d configurations..." %(allconfigs,)

        curconfig = 0
        self.wConfigs = {}
        for source in self.sources:
            self.wConfigs[source] = None

        #second round of iterating, test for each configuration
        interrupt = False
        for self.source in self.sources:
           if(interrupt):
               break
           for self.ctcap in self.ctcaps:
               if(interrupt):
                   break
               for self.version in self.versions:
                   if(interrupt):
                       break
                   for self.identifier in self.identifiers:
                       if(interrupt):
                           break
                       for self.uri in self.uris[self.source]:
                           if(interrupt):
                               break
                           for self.type in self.types[self.source]:
                               curconfig +=1
                               skip = self.prepare (allconfigs, curconfig)
                               if (not skip):
                                   (status, interrupt) = self.testWithCurrentConfiguration ()
                                   if (status and not interrupt):
                                       self.wConfigs[self.source] = ConfigurationParameter (self.version, self.source, self.uri, self.type, self.ctcap, self.identifier)
                                       print "Found a working configuration for %s" % (self.source,)
                                       if (options.verbose > 0):
                                           self.wConfigs[self.source].printMe()
                               if (interrupt):
                                   break;
        if(interrupt):
            print "Test Interrupted"
            return 1

        print "Test Ended"

        #Test finished, print summary and generating configurations
        print "****************SUMMARY****************"
        found = False
        for source,config in self.wConfigs.items():
            if (config):
                found = True
                print "------------------------------------------"
                print "Configuration parameter for %s:" % (source,)
                config.printMe()

        if (not found):
            print "No working configuration found"
        else:
            have_combined = \
                self.wConfigs.has_key('calendar') and \
                self.wConfigs.has_key('todo') and \
                self.wConfigs['calendar'] and \
                self.wConfigs['todo'] and \
                self.wConfigs['calendar'].uri == self.wConfigs['todo'].uri

            if (options.create):
                #first remove the previous configuration if there is a configuration with the same name
                create = options.create
                cmd = "%s --remove '%s'" %(syncevoCmd, create)
                try:
                    runCommand (cmd)
                except:
                    pass
                cmd = "%s -c --template 'SyncEvolution Client' --sync-property peerIsClient=1 %s" %(syncevoCmd, create)
                runCommand (cmd)
                #disable all sources by default
                for source in allSources:
                    if source == 'calendar+todo':
                        continue
                    cmd = "%s -c --source-property sync='disabled' %s %s" %(syncevoCmd, create, source)
                    runCommand(cmd)

                syncCreated = False
                for source,config in self.wConfigs.items():
                    if (config):
                        if (not syncCreated):
                            #set the sync parameter
                            cmd = "%s --configure --sync-property syncURL='obex-bt://%s' --sync-property remoteIdentifier='%s' --sync-property SyncMLVersion='%s' '%s'" %(syncevoCmd, self.btaddr, config.identifier, config.version, create)
                            syncCreated = True
                            runCommand (cmd)
                        #set each source parameter
                        ltype = strip_version(config.type)
                        cmd = "%s --configure --source-property sync='two-way' --source-property URI='%s' --source-property type='%s:%s' '%s' '%s'" %(syncevoCmd, config.uri, source, ltype, create, config.source)
                        runCommand(cmd)
                if have_combined:
                    ltype = strip_version(self.wConfigs['calendar'].type)
                    uri = self.wConfigs['calendar'].uri
                    cmd = "%s --configure --source-property evolutionsource='calendar,todo' --source-property sync='two-way' --source-property URI='%s' --source-property type='virtual:%s' '%s' calendar+todo" %(syncevoCmd, uri, ltype, create)
                    runCommand(cmd)
                    for source in ('calendar', 'todo'):
                        cmd = "%s --configure --source-property sync='none' --source-property URI='%s' '%s' %s" %(syncevoCmd, uri, create, source)
                        runCommand(cmd)


            if (options.advanced):
                print ""
                print "We have conducted basic test by sending and receiving"
                print "data to the phone. You can help the SyncEvolution project"
                print "and other users by submitting the following configuration"
                print "template at http://syncevolution.org/wiki/phone-compatibility-template"
                print ""

                configini = { "peerIsClient": "1" }
                sourceConfigInis = {}

                for source,config in self.wConfigs.items():
                    if(config):
                        sourceini = {}
                        if (config.identifier):
                            configini["remoteIdentifier"] = config.identifier
                        if (config.version != '1.2'):
                            configini["SyncMLVersion"] = config.version
                        sourceini["sync"] = "two-way"
                        sourceini["uri"] = config.uri
                        sourceini["backend"] = source
                        sourceini["syncFormat"] = strip_version(config.type)
                        sourceConfigInis[source] = sourceini

                # create 'calendar+todo' entry, disable separate 'calendar' and 'todo'?
                if have_combined:
                    sourceini = {}
                    sourceini["sync"] = "two-way"
                    sourceini["database"] = "calendar,todo"
                    sourceini["uri"] = self.wConfigs['calendar'].uri
                    sourceini["backend"] = "virtual"
                    sourceini["syncFormat"] = strip_version(self.wConfigs['calendar'].type)
                    sourceConfigInis['calendar+todo'] = sourceini
                    # disable the sub datasources
                    for source in ('calendar', 'todo'):
                        sourceConfigInis[source]["sync"] = "none"
                        sourceConfigInis[source].pop("uri")

                # print template to stdout
                sep = "--------------------> snip <--------------------"
                print sep
                print "=== template.ini ==="
                print "fingerprint = <Model> <Manufacturer>"
                print "=== config.ini ==="
                print hash2ini(configini)
                print "consumerReady = 1"
                for source, configini in sourceConfigInis.items():
                    print "=== sources/%s/config.ini ===" % source
                    print hash2ini(configini)
                print sep
            else:
                print ""
                print "We just conducted minimum test by syncing with the phone"
                print "without checking received data. For more reliable result,"
                print "use the --advanced option, but beware that it will overwrite"
                print "contacts, events, tasks and memos on the phone."

            if (options.create):
                print ""
                print "Created configuration: %s" %(options.create)
                print "You may start syncing with: syncevolution %s" %(options.create)

def main():
    versions = []
    sources = []
    ctcaps = []
    identifiers = []
    uris = {}
    types = {}
    if (options.version):
        versions.append (options.version)
    if (options.source):
        sources.append (options.source)
    if (options.uri):
        uris[sources[0]] = []
        uris[sources[0]].append(options.uri)
    if (options.type):
        types[sources[0]] = []
        types[sources[0]].append(options.type)
    if (options.ctcap):
        ctcaps.append (options.ctcap)
    if (options.identifier):
        identifiers.append (options.identifier)

    config = TestingConfiguration (versions, sources, uris, types, ctcaps,
            identifiers, options.btaddr)

    tmpdir = tempfile.mkdtemp(prefix="syncevo-phone-config")
    global testFolder
    global testResult
    global testConfig
    testFolder = tmpdir+'/data'
    testResult = tmpdir+'/cache'
    testConfig = tmpdir+'/config'
    print "Running test with test data inside %s and test results inside %s" %(testFolder, testResult)
    config.run()

if __name__ == "__main__":
  main()
