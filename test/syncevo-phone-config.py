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
import sys, optparse, os, time, popen2, tempfile
import shutil
import ConfigParser
import glob

########################### cmdline options ##########################################
parser = optparse.OptionParser()
parser.add_option("-b", "--bt-address", action = "store", type = "string",
        dest = "btaddr", help = "The Bluetooth mac address for the testing phone", default = "")
parser.add_option ("-p", "--proto-version", action = "store", type = "string",
        dest="version", help = "The SyncML protocal version for testing, can be one of 1.0|1.1|1.2, by default it will try all versions one by one"
        ,default = "")
parser.add_option ("-s", "--source", action = "store", type = "string", dest=
        "source", help = "The local database for testing, can be one of contact|calendar|task|memo|calendar+task, by default it will try all except calendar+task, use --combined-calendar-task to activate",
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
parser.add_option ("-v", "--verbose", action = "store_true", default = False,
        dest = "verbose", help = "Enabling detailed output")
parser.add_option ("", "--combined-calendar-task", action = "store_true",
        default = False, dest = "combined", help = "Testing the combined calendar, task data source")
parser.add_option ("-a", "--advanced", action = "store_true", default = False,
        dest = "advanced", help = "More extensive test with sending/receving data, WARNING: will destroy your data on the tested phone")
parser.add_option("-c", "--create-config", action ="store", type = "string", 
        dest = "create", help = "If set, a configuration file with the name will be created based on the testing result",
        default ="")
parser.add_option("-l", "--create-template", action ="store", type = "string", 
        dest = "template", help = "If set, a template for the found configuration with the name will be created, it is a folder located in current working directory",default ="")
(options, args) = parser.parse_args()

####################semantic check for  cmdline options #######################################
if (not options.btaddr):
    parser.error ("Please input the bluetooth address for the testing phone by -b")
if (options.version and options.version not in ['1.1', '1.2', '1.3']):
    parser.error("option -p can only be one of 1.0|1.1|1.2")
if (options.source and options.source not in ['contact', 'calendar', 'task',
    'memo', 'calendar+task']):
    parser.error("option -s can only be one of contact|calendar|task|memo|calendar+task")
if (options.uri and not options.source and not options.combined):
    parser.error ("options -u must work with -s")
if (options.type and not options.source):
    parser.error ("options -t must work with -s")

#######################some global parameters ######################
syncevo = 'syncevolution'
configName = 'bfb3e7cb3d259e5f5aabbfb2ffac23f8cf5ad91b'
configContext = 'test-phone'
templateName = '"Nokia 7210c"'
testFolder = None
testResult = None

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

    def equalWith(self, config):
        return (config and \
                self.ctcap == config.ctcap and \
                self.identifier == config.identifier and \
                self.version == config.version and \
                self.uri == config.uri and \
                self.type == config.type)

###################### utility functions ####################
def isCombinedSource (source):
    return source == 'calendar+task'

def getSubSources (source):
    return ['calendar','task']

def clearLocalSyncData(sources):
    for source in sources:
        dirname = "%s/%s" % (testFolder, source)
        rm_r(dirname)
        os.makedirs(dirname)

def insertLocalSyncData(sources, type):
    for source in sources:
        testcase = getTestCase (source, type)
        cmd = "echo \"%s\" > %s/%s/0 ;echo 'insertLocalSyncData'" % (testcase, testFolder, source)
        runCommand(cmd)

def getTestCase(source, type):
    if (source == 'contact' and (type == 'text/vcard:3.0' or type == 'text/vcard')):
        return  "BEGIN:VCARD\n"\
        +"VERSION:3.0\n"\
        +"TITLE:tester\n"\
        +"FN:John Doe\n"\
        +"N:Doe;John;;;\n"\
        +"TEL;TYPE=WORK;TYPE=VOICE:business 1\n"\
        +"X-EVOLUTION-FILE-AS:Doe\\, John\n"\
        +"X-MOZILLA-HTML:FALSE\n"\
        +"NOTE:test-phone\n"\
        +"END:VCARD\n"
    if (source == 'contact' and (type == 'text/x-vcard:2.1' or type == 'text/x-vcard')):
        return "BEGIN:VCARD\n"\
        +"VERSION:2.1\n"\
        +"TITLE:tester\n"\
        +"FN:John Doe\n"\
        +"N:Doe;John;;;\n"\
        +"TEL;TYPE=WORK;TYPE=VOICE:business 1\n"\
        +"X-MOZILLA-HTML:FALSE\n"\
        +"NOTE:REVISION\n"\
        +"END:VCARD\n"
    if (source == 'calendar' and (type == 'text/calendar:2.0' or type == 'text/calendar')):
        return "BEGIN:VCALENDAR\n"\
        +"PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"\
        +"VERSION:2.0\n"\
        +"METHOD:PUBLISH\n"\
        +"BEGIN:VEVENT\n"\
        +"SUMMARY:phone meeting\n"\
        +"DTEND:20060406T163000Z\n"\
        +"DTSTART:20060406T160000Z\n"\
        +"DTSTAMP:20060406T211449Z\n"\
        +"LAST-MODIFIED:20060409T213201\n"\
        +"CREATED:20060409T213201\n"\
        +"LOCATION:my office\n"\
        +"DESCRIPTION:let's talkREVISION\n"\
        +"END:VEVENT\n"\
        +"END:VCALENDAR\n"
    if (source == 'calendar' and (type =='text/x-vcalendar:1.0' or type == 'text/x-vcalendar')):
        return "BEGIN:VCALENDAR\n"\
        +"VERSION:1.0\n"\
        +"BEGIN:VEVENT\n"\
        +"SUMMARY:phone meeting\n"\
        +"DTEND:20060406T163000Z\n"\
        +"DTSTART:20060406T160000Z\n"\
        +"DTSTAMP:20060406T211449Z\n"\
        +"LOCATION:my office\n"\
        +"DESCRIPTION:let's talkREVISION\n"\
        +"END:VEVENT\n"\
        +"END:VCALENDAR\n"
    if (source == 'task' and (type == 'text/calendar:2.0' or type == 'text/calendar')):
        return "BEGIN:VCALENDAR\n"\
        +"PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"\
        +"VERSION:2.0\n"\
        +"METHOD:PUBLISH\n"\
        +"BEGIN:VTODO\n"\
        +"DTSTAMP:20060417T173712Z\n"\
        +"SUMMARY:do me\n"\
        +"DESCRIPTION:to be doneREVISION\n"\
        +"CREATED:20060417T173712\n"\
        +"LAST-MODIFIED:20060417T173712\n"\
        +"END:VTODO\n"\
        +"END:VCALENDAR\n"
    if (source == 'task' and (type == 'text/x-vcalendar:1.0' or type == 'text/x-vcalendar')):
        return "BEGIN:VCALENDAR\n"\
        +"PRODID:-//Ximian//NONSGML Evolution Calendar//EN\n"\
        +"VERSION:1.0\n"\
        +"METHOD:PUBLISH\n"\
        +"BEGIN:VTODO\n"\
        +"DTSTAMP:20060417T173712Z\n"\
        +"SUMMARY:do me\n"\
        +"DESCRIPTION:to be doneREVISION\n"\
        +"CREATED:20060417T173712\n"\
        +"LAST-MODIFIED:20060417T173712\n"\
        +"END:VTODO\n"\
        +"END:VCALENDAR\n"
    if (source == 'memo'):
        return "SUMMARY\n"\
        +"BODY TEXT\n"
    return ""

# Get the keyword to be matched with for each test case
def getTestCaseKeywords(source, type):
    if (source == 'contact' and (type == 'text/vcard:3.0' or type == 'text/vcard')):
        return ["VCARD", "TITLE:tester", "Doe", "John"] 
    if (source == 'contact' and (type == 'text/x-vcard:2.1' or type == 'text/x-vcard')):
        return ["VCARD", "TITLE:tester", "Doe", "John"]
    if (source == 'calendar' and (type == 'text/calendar:2.0' or type == 'text/calendar')):
        return ["VCALENDAR", "VEVENT", "phone meeting", "my office"]
    if (source == 'calendar' and (type =='text/x-vcalendar:1.0' or type == 'text/x-vcalendar')):
        return ["VCALENDAR", "VEVENT", "phone meeting", "my office"]
    if (source == 'task' and (type == 'text/calendar:2.0' or type == 'text/calendar')):
        return ["VCALENDAR", "VTODO", "do me"]
    if (source == 'task' and (type == 'text/x-vcalendar:1.0' or type == 'text/x-vcalendar')):
        return ["VCALENDAR", "VTODO", "do me"]
    if (source == 'memo'):
        return ["SUMMARY"]
    return ""

# Compare the received data with the sent data, we only match selected keywords in received
# data for a basic sanity test
def compareSyncData(sources, type):
    for source in sources:
        testcase = getTestCase (source, type)
        received = ''
        recFile = "%s/%s/0" %(testFolder, source)
        try:
            rf = open (recFile)
            received = rf.read()
        except:
            return False
        keys = getTestCaseKeywords(source, type)
        if (options.verbose):
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
    if (options.verbose):
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

##############################TestConfiguration##################################
class TestingConfiguration():
    def __init__(self, versions, sources, uris, types, ctcaps, identifiers, btaddr):
        self.allSources = ['contact', 'calendar', 'task', 'memo', 'calendar+task']
        if (versions):
            self.versions = versions
        else:
            self.versions = ["1.2", "1.1", "1.0"]
        if (sources):
            self.sources = sources
        else:
            self.sources = ["contact", "calendar", "task", "memo"]
            if (options.combined):
                self.sources.insert (0, "calendar+task")
        if (uris):
            self.uris = uris
        else:
            self.uris = {}
            self.uris['contact'] = ['Contact', 'contact', 'Contacts', 'contacts', 'Addressbook', 'addressbook']
            self.uris['calendar'] = ['Calendar', 'calendar', 'Agenda','agenda']
            self.uris['task'] = self.uris['calendar'] + ['Task', 'task', 'Tasks', 'tasks', 'Todo','todo']
            self.uris['memo'] = ['Memo', 'memo', 'Notes', 'notes', 'Note', 'note']
            self.uris['calendar+task'] = self.uris['calendar'] + self.uris['task']
        if (types):
            self.types = types
        else:
            self.types = {}
            self.types['contact'] = ['text/vcard:3.0', 'text/x-vcard:2.1']
            self.types['calendar'] = self.types['task'] = ['text/calendar:2.0', 'text/x-vcalendar:1.0']
            self.types['memo'] = ['text/plain:1.0',
                    'text/calendar:2.0', 'text/x-vcalendar:1.0']
            self.types['calendar+task'] = self.types['calendar']

        if (ctcaps):
            self.ctcaps = ctcaps
        else:
            self.ctcaps =  [True, False]
        if (identifiers):
            self.identifiers = identifiers
        else:
            self.identifiers = ['PC Suite','','Nokia PC Suite']
        self.btaddr = btaddr


    #map between user perceivable source name and the underlying source name in SyncEvolution
    def getLocalSourceName (self, source):
        if (source == 'contact'):
            return 'addressbook'
        if (source == 'calendar'):
            return 'calendar'
        if (source == 'task'):
            return 'todo'
        if (source == 'memo'):
            return 'memo'
        if (source =='calendar+task'):
            return 'calendar+todo'

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
                if ((self.source == 'calendar' or self.source == 'task') and isCombinedSource(config.source)):
                    skip = True
        if (skip):
            if (options.verbose):
                print "Test %d/%d skipped because already found a working configuration" % (curconfig, allconfigs)
            else:
                print "Test %d/%d skipped" %(curconfig, allconfigs)
        else:
            print "Start %d/%d test" % (curconfig, allconfigs)
            if (options.verbose):
                config = ConfigurationParameter(self.version, self.source, self.uri, self.type, self.ctcap, self.identifier)
                config.printMe()
        return skip


    #run the real sync test with current configuration parameter
    #if advanced option is set and the basic test succeed, it will contintue with 
    #the advanced test
    def testWithCurrentConfiguration(self):
        """ Prepare the configuration and run a sync session, Returns true if
        the test was successful, otherwise false"""
        fullConfigName = configName +'@' + configContext
        try:
            runCommand (syncevo+' --remove '+fullConfigName)
        except:
            pass
        runCommand (syncevo+' -c -l ' + templateName + ' ' + fullConfigName)
        runSources ={'contact':'addressbook', 'calendar':'calendar', 'task':'todo', 'memo':'memo', 'calendar+task':'calendar+todo'}
        # set the local database
        if (isCombinedSource(self.source)):
            for s in getSubSources(self.source):
                    filesource = testFolder+'/'+s
                    configCmd = "%s --configure --source-property evolutionsource='file:///%s' %s %s" %(syncevo,filesource, fullConfigName, runSources[s]) 
                    runCommand(configCmd)
            subSources = getSubSources(self.source)
            filesource = runSources[subSources[0]] +',' + runSources[subSources[1]]
            configCmd = "%s --configure --source-property evolutionsource=%s %s %s" %(syncevo,filesource, fullConfigName, runSources[self.source]) 
            runCommand(configCmd)

        else:
            filesource = testFolder+'/'+self.source
            configCmd = "%s --configure --source-property evolutionsource='file:///%s' %s %s" %(syncevo,filesource, fullConfigName, runSources[self.source]) 
            runCommand (configCmd)

        configCmd = "%s --configure --sync-property logLevel=5 --sync-property SyncURL=obex-bt://%s --sync-property SyncMLVersion=%s %s" % (syncevo, self.btaddr,self.version, fullConfigName)
        runCommand (configCmd)

        if (self.identifier):
            configCmd = "%s --configure --sync-property remoteIdentifier='%s' %s" %(syncevo, self.identifier, fullConfigName)
            runCommand (configCmd)

        if (isCombinedSource(self.source)):
            configCmd = "%s --configure --source-property type=%s --source-property uri=%s %s %s" %(syncevo, "virtual:"+self.type.partition(':')[0], self.uri, fullConfigName, runSources[self.source])
            runCommand (configCmd)
            for s in getSubSources(self.source):
                configCmd = "%s --configure --source-property type=%s %s %s" %(syncevo, "file:"+self.type, fullConfigName, runSources[s])
                runCommand (configCmd)
        else:
            configCmd = "%s --configure --source-property type=%s --source-property uri=%s %s %s" %(syncevo, "file:"+self.type, self.uri, fullConfigName, runSources[self.source])
            runCommand (configCmd)

        """ start the sync session """
        cmdPrefix="XDG_CACHE_HOME=%s " %(testResult)
        if (not self.ctcap):
            cmdPrefix += "SYNCEVOLUTION_NOCTCAP=t "
        syncCmd = "%s %s %s" % (syncevo, fullConfigName, runSources[self.source])
        (status,interrupt) = runSync (cmdPrefix+syncCmd)
        if (options.advanced and status and not interrupt):
            (status,interrupt)= self.advancedTestWithCurrentConfiguration(runSources)
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
    def advancedTestWithCurrentConfiguration (self, runSources):
        """ 
        Sending/receving real data for basic sanity test
        """
        fullConfigName = configName +'@' + configContext
        sources = []
        if (isCombinedSource (self.source)):
            sources = getSubSources (self.source)
        else:
            sources.append(self.source)

        #step 1: clean the data both locally and remotely using a 'slow-sync' and 'two-way'
        clearLocalSyncData(sources)
        cmdPrefix="XDG_CACHE_HOME=%s " % (testResult)
        if (not self.ctcap):
            cmdPrefix += "SYNCEVOLUTION_NOCTCAP=t "
        syncCmd = "%s %s --sync slow %s %s" % (cmdPrefix, syncevo, fullConfigName, runSources[self.source])
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)
        clearLocalSyncData(sources)
        syncCmd = "%s %s --sync two-way %s %s" % (cmdPrefix, syncevo, fullConfigName, runSources[self.source])
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)

        #step 2: insert testcase to local data and sync with 'two-way'
        insertLocalSyncData(sources, self.type)
        syncCmd = "%s %s --sync two-way %s %s" % (cmdPrefix, syncevo, fullConfigName, runSources[self.source])
        status,interrupt = runSync(syncCmd)
        if (not status or interrupt):
            return (status, interrupt)

        #step 3: delete local data and sync with 'slow-sync'
        clearLocalSyncData(sources)
        syncCmd = "%s %s --sync slow %s %s" % (cmdPrefix, syncevo, fullConfigName, runSources[self.source])
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
                                       if (options.verbose):
                                           self.wConfigs[self.source].printMe()
                               if (interrupt):
                                   break;
        if(interrupt):
            print "Test Interrupted"
            self.cleanup()
            return 1

        print "Test Ended"
        self.cleanup()

        #Test finished, print summary and generating configurations
        print "****************SUMMARY****************"
        found = False
        for source,config in self.wConfigs.items():
            if (config):
                found = True
                print "------------------------------------------"
                print "Configuration parameter for %s:" % (source,)
                config.printMe()

        #We did not tested with calendar+task but we have found configuration 
        #for calendar or task
        if (options.combined == False):
            s = getSubSources(self.source)
            try:
                if (self.wConfigs[s[0]] or self.wConfigs[s[1]]):
                    print ""
                    print "Note some phones have combined calendar and task"
                    print "you might run the tool with --combined-calendar-task"
                    print "to detect this behavior"
                #restrict to only two subsources
                if (self.wConfigs[s[0]] and self.wConfigs[s[0]].equalWith (self.wConfigs[s[1]])):
                    print "This phone likely works with a combined calendar and task"
                    print "because they have identical configurations"
            except:
                pass

        if (not found):
            print "No working configuration found"
        else:
            if (options.create):
                #first remove the previous configuration if there is a configuration with the same name
                create = options.create
                cmd = "%s --remove '%s'" %(syncevo, create)
                try:
                    runCommand (cmd)
                except:
                    pass
                #create the configuration based on the template
                cmd = "%s -c -l %s %s" %(syncevo, templateName, create)
                runCommand (cmd)
                #disable all sources by default
                for source in self.allSources:
                    cmd = "%s -c --source-property sync='disabled' %s %s" %(syncevo, create, self.getLocalSourceName(source))
                    runCommand(cmd)

                syncCreated = False
                for source,config in self.wConfigs.items():
                    if (config):
                        if (not syncCreated):
                            #set the sync parameter
                            cmd = "%s --configure --sync-property syncURL='obex-bt://%s' --sync-property remoteIdentifier='%s' --sync-property SyncMLVersion='%s' '%s'" %(syncevo, self.btaddr, config.identifier, config.version, create)
                            syncCreated = True
                            runCommand (cmd)
                        #set each source parameter
                        ltype = config.type.split(':')[0]
                        if(isCombinedSource (config.source)):
                            ltype = 'virtual:'+ltype
                        cmd = "%s --configure --source-property sync='two-way' --source-property URI='%s' --source-property type='%s' '%s' '%s'" %(syncevo, config.uri, ltype, create, self.getLocalSourceName(config.source))
                        runCommand(cmd)

            if (options.template):
                template = options.template
                configini = "peerIsClient = 1\n"
                sourceConfiginis={}
                syncCreated = False
                for source,config in self.wConfigs.items():
                    if(config):
                        if (not syncCreated):
                            if (config.identifier):
                                configini += "remoteIdentifier = '%s'\n" %(config.identifier)
                            if (config.version != '1.2'):
                                configini += "SyncMLVersion = '%s'\n" %(config.version)
                            syncCreated = True
                        sourceini = "sync = two-way\n"
                        if (isCombinedSource (source)):
                            sourceini += "evolutionsource = calendar,todo\n"
                            sourceini += "uri = %s\n" %(config.uri)
                            sourceini += "type = virtual:%s\n" %(config.type.split(':')[0])
                            sourceConfiginis[self.getLocalSourceName(source)]=sourceini
                            #disable the sub datasoruce
                            for s in getSubSources(source):
                                sourceini = "sync = none\n"
                                sourceConfiginis[self.getLocalSourceName(s)]=sourceini
                        else:
                            sourceini += "uri = %s\n" %(config.uri)
                            sourceConfiginis[self.getLocalSourceName(source)]=sourceini
                            defualtTypes = ["text/x-vcard:2.1", "text/calendar:2.0", "text/plain:1.0"]
                            if (config.type not in defualtTypes):
                                sourceini += "type = %s\n" %(config.type)

                fingerprint=''
                description=''
                templateini = "fingerprint = %s\ndescription = %s\n" %(fingerprint,description)
                #write to directory
                shutil.rmdir(template)
                os.makedirs("%s/sources" % template)
                runCommand(cmd)
                cmd = "echo '%s' >%s/config.ini; echo ''" %(configini, template)
                runCommand(cmd)
                cmd = "echo '%s' >%s/template.ini; echo ''" %(templateini, template)
                runCommand(cmd)
                for s,ini in sourceConfiginis.items():
                    cmd = "mkdir %s/sources/%s; echo''" %(template, s)
                    runCommand(cmd)
                    cmd = "echo '%s' >%s/sources/%s/config.ini; echo ''" %(ini,template,s)
                    runCommand(cmd)

            if (options.advanced):
                print ""
                print "We have conducted basic test by sending and receiving"
                print "data for the phone"
            else:
                print ""
                print "We just conducted minimum test by syncing with the phone"
                print "without checking received data. You can use --advanced"
                print "to conduct basic sending/receiving test against your phone"

            if (options.create):
                print "Created configuration %s" %(options.create)
                print "You may start syncing with syncevolution %s" %(options.create)

            if (options.template):
                print "Created configuration template %s" %(options.template)
                print "It is located as folder %s in current directory" %(options.template)

    def cleanup (self):
        #remove configurations 
        fullConfigName = configName +'@' + configContext
        cleanCmd = '%s --remove "%s"' %(syncevo, fullConfigName,)
        try:
            runCommand (cleanCmd)
        except:
            pass

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
    testFolder = tmpdir+'/data'
    testResult = tmpdir+'/cache'
    print "Running test with test data inside %s and test results inside %s" %(testFolder, testResult)
    config.run()

if __name__ == "__main__":
  main()
