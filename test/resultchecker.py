#!/usr/bin/python

'''
 Copyright (C) 2009 Intel Corporation

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) version 3.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 02110-1301  USA
'''
import sys,os,glob,datetime,popen2
import re

""" 
resultcheck.py: tranverse the test result directory, generate an XML
based test report.
"""

# sort more accurately on sub-second modification times
os.stat_float_times(True)

space="  "
def check (resultdir, serverlist,resulturi, srcdir, shellprefix, backenddir):
    '''Entrypoint, resutldir is the test result directory to be generated,
    resulturi is the http uri, it will only process corresponding server's
    test results list in severlist'''
    if serverlist:
        servers = serverlist.split(",")
    else:
        servers = []
    result = open("nightly.xml","w")
    result.write('''<?xml version="1.0" encoding="utf-8" ?>\n''')
    result.write('''<nightly-test>\n''')
    indents=[space]
    if(os.path.isfile(resultdir+"/output.txt")==False):
        print "main test output file not exist!"
    else:
        indents,cont = step1(resultdir+"/output.txt",result,indents,resultdir,resulturi, shellprefix, srcdir)
        if (cont):
            step2(resultdir,result,servers,indents,srcdir,shellprefix,backenddir)
        else:
            # compare.xsl fails if there is no <client-test> element:
            # add an empty one
            result.write('''<client-test/>\n''')
    result.write('''</nightly-test>\n''')
    result.close()

def step1(input, result, indents, dir, resulturi, shellprefix, srcdir):
    '''Step1 of the result checking, collect system information and 
    check the preparation steps (fetch, compile)'''
    cont = True
    indent =indents[-1]+space
    indents.append(indent)
    result.write(indent+'''<platform-info>\n''')
    indent =indents[-1]+space
    indents.append(indent)
    result.write(indent+'''<cpuinfo>\n''')
    fout,fin=popen2.popen2('cat /proc/cpuinfo|grep "model name" |uniq')
    s = fout.read()
    result.write(indent+s)
    result.write(indent+'''</cpuinfo>\n''')
    result.write(indent+'''<memoryinfo>\n''')
    fout,fin=popen2.popen2('cat /proc/meminfo|grep "Mem"')
    for s in fout:
        result.write(indent+s)
    result.write(indent+'''</memoryinfo>\n''')
    result.write(indent+'''<osinfo>\n''')
    fout,fin=popen2.popen2('uname -osr')
    s = fout.read()
    result.write(indent+s)
    result.write(indent+'''</osinfo>\n''')
    if 'schroot' in shellprefix:
        result.write(indent+'''<chrootinfo>\n''')
        fout,fin=popen2.popen2(shellprefix.replace('schroot', 'schroot -i'))
        s = ""
        for line in fout:
            if line.startswith("  Name ") or line.startswith("  Description "):
                 s = s + line
        result.write(indent+s)
        result.write(indent+'''</chrootinfo>\n''')
    result.write(indent+'''<libraryinfo>\n''')
    libs = ['libsoup-2.4', 'evolution-data-server-1.2', 'glib-2.0','dbus-glib-1']
    s=''
    #change to a dir so that schroot will change to an available directory, without this
    #schroot will fail which in turn causes the following cmd has no chance to run
    oldpath = os.getcwd()  
    tmpdir = srcdir
    while (os.path.exists(tmpdir) == False):
        tmpdir = os.path.dirname(tmpdir)
    os.chdir(tmpdir)
    for lib in libs:
        fout,fin=popen2.popen2(shellprefix+' pkg-config --modversion '+lib +' |grep -v pkg-config')
        s = s + lib +': '+fout.read() +'  '
    os.chdir(oldpath)
    result.write(indent+s)
    result.write(indent+'''</libraryinfo>\n''')
    indents.pop()
    indent = indents[-1]
    result.write(indent+'''</platform-info>\n''')
    result.write(indent+'''<prepare>\n''')
    indent =indent+space
    indents.append(indent)
    tags=['libsynthesis', 'syncevolution', 'compile', 'dist']
    tagsp={'libsynthesis':'libsynthesis-fetch-config',
            'syncevolution':'syncevolution-fetch-config','compile':'compile','dist':'dist'}
    for tag in tags:
        result.write(indent+'''<'''+tagsp[tag])
        fout,fin=popen2.popen2('find `dirname '+input+'` -type d -name *'+tag)
        s = fout.read().rpartition('/')[2].rpartition('\n')[0]
        result.write(' path ="'+s+'">')
	'''check the result'''
        if(not os.system("grep -q '^"+tag+".* disabled in configuration$' "+input)):
            result.write("skipped")
        elif(os.system ("grep -q '^"+tag+" successful' "+input)):
            result.write("failed")
            cont = False
        else:
            result.write("okay")
        result.write('''</'''+tagsp[tag]+'''>\n''')
    indents.pop()
    indent = indents[-1]
    result.write(indent+'''</prepare>\n''')
    result.write(indent+'''<log-info>\n''')
    indent =indent+space
    indents.append(indent)
    result.write(indent+'''<uri>'''+resulturi+'''</uri>\n''')
    indents.pop()
    indent = indents[-1]
    result.write(indent+'''</log-info>\n''')
    indents.pop()
    indent = indents[-1]
    return (indents, cont)

def step2(resultdir, result, servers, indents, srcdir, shellprefix, backenddir):
    '''Step2 of the result checking, for each server listed in
    servers, tranverse the corresponding result folder, process
    each log file to decide the status of the testcase'''
    '''Read the runtime parameter for each server '''
    params = {}
    if servers:
        cmd='sed -n '
        for server in servers:
            cmd+= '-e /^'+server+'/p '
        fout,fin=popen2.popen2(cmd +resultdir+'/output.txt')
        for line in fout:
            for server in servers:
                # find first line with "foobar successful" or "foobar: <command failure>"
                if (line.startswith(server + ":") or line.startswith(server + " ")) and server not in params:
                    t = line.partition(server)[2].rpartition('\n')[0]
                    if(t.startswith(':')):
                        t=t.partition(':')[2]
                    params[server]=t
    
    indent =indents[-1]+space
    indents.append(indent)
    '''start of testcase results '''
    result.write(indent+'''<client-test>\n''')
    runservers = os.listdir(resultdir)
    #list source test servers statically, we have no idea how to differenciate
    #automatically whether the server is a source test or sync test.
    sourceServers = ['evolution',
                     'evolution-prebuilt-build',
                     'yahoo',
                     'davical',
                     'googlecalendar',
                     'apple',
                     'dbus']
    sourceServersRun = 0
    haveSource = False
    #Only process servers listed in the input parameter and in the sourceServer
    #list and have a result folder
    for server in servers:
        matched = False
        for rserver in runservers:
            for source in sourceServers:
                if (rserver.find('-')!=-1 and server == rserver.partition('-')[2] and server == source):
                    matched = True
                    break
        if(matched):
            #put the servers at the front of the servers list, so that we will
            #process test first
            servers.remove(server)
            servers.insert (0, server);
            sourceServersRun = sourceServersRun+1;
            haveSource = True

    #process source tests first 
    if (haveSource) :
        indent +=space
        indents.append(indent)
        result.write(indent+'''<source>\n''')

    haveSync = False
    for server in servers:
        matched = False
	'''Only process servers listed in the input parametr'''
        for rserver in runservers:
            if(rserver.find('-')!= -1 and rserver.partition('-')[2] == server):
                matched = True
                break;
        if(matched):
            sourceServersRun = sourceServersRun -1;
            if (sourceServersRun == -1):
                haveSync = True
                '''generate a template which lists all test cases we supply, this helps 
                generate a comparable table and track potential uncontentional skipping
                of test cases'''
                templates=[]
                oldpath = os.getcwd()
                # Get list of Client::Sync tests one source at a time (because
                # the result might depend on CLIENT_TEST_SOURCES and which source
                # is listed there first) and combine the result for the common
                # data types (because some tests are only enable for contacts, others
                # only for events).
                # The order of the tests matters, so don't use a hash and start with
                # a source which has only the common tests enabled. Additional tests
                # then get added at the end.
                for source in ('file_task', 'file_event', 'file_contact', 'eds_contact', 'eds_event'):
                    os.chdir (srcdir)
                    fout,fin=popen2.popen2(shellprefix + " env LD_LIBRARY_PATH=build-synthesis/src/.libs SYNCEVOLUTION_BACKEND_DIR="+backenddir +" CLIENT_TEST_SOURCES="+source+" ./client-test -h")
                    os.chdir(oldpath)
                    for line in fout:
                        l = line.partition('Client::Sync::'+source+'::')[2].rpartition('\n')[0]
                        if l != '' and l not in templates:
                            templates.append(l)
                indent +=space
                indents.append(indent)
                result.write(indent+'<sync>\n')
                result.write(indent+space+'<template>')
                for template in templates:
                    result.write(indent+space+'<'+template+'/>')
                result.write('</template>\n')
            indent +=space
            indents.append(indent)
            result.write(indent+'<'+server+' path="' +rserver+'" ')
            #valgrind check resutls
            if(params[server].find('return code ') !=-1):
                result.write('result="'+params[server].partition('return code ')[2].partition(')')[0]+'" ')
            result.write('>\n')
            # sort files by creation time, to preserve run order
            logs = map(lambda file: (os.stat(file).st_mtime, file),
                       glob.glob(resultdir+'/'+rserver+'/*.log'))
            logs.sort()
            logs = map(lambda entry: entry[1], logs)
            logdic ={}
            logprefix ={}
            if server == 'dbus':
                # Extract tests and their results from output.txt,
                # which contains Python unit test output. Example follows.
                # Note that there can be arbitrary text between the test name
                # and "ok" resp. "FAIL/ERROR". Therefore failed tests
                # are identified not by those words but rather by the separate
                # error reports at the end of the output. Those reports
                # are split out into separate .log files for easy viewing
                # via the .html report.
                #
                # TestDBusServer.testCapabilities - Server.Capabilities() ... ok
                # TestDBusServer.testGetConfigScheduleWorld - Server.GetConfigScheduleWorld() ... ok
                # TestDBusServer.testGetConfigsEmpty - Server.GetConfigsEmpty() ... ok
                # TestDBusServer.testGetConfigsTemplates - Server.GetConfigsTemplates() ... FAIL
                # TestDBusServer.testInvalidConfig - Server.NoSuchConfig exception ... ok
                # TestDBusServer.testVersions - Server.GetVersions() ... ok
                #
                #======================================================================
                # FAIL: TestDBusServer.testGetConfigsTemplates - Server.GetConfigsTemplates()
                # ---------------------------------------------------------------------

                # first build list of all tests, assuming that they pass
                dbustests = {}
                test_start = re.compile(r'''^Test(?P<cl>.*)\.test(?P<func>[^ ]*)''')
                test_fail = re.compile(r'''(?P<type>FAIL|ERROR): Test(?P<cl>.*)\.test(?P<func>[^ ]*)''')
                logfile = None
                sepcount = 0
                for line in open(rserver + "/output.txt"):
                    m = test_start.search(line)
                    if m:
                        is_okay = True
                    else:
                        m = test_fail.search(line)
                        is_okay = False
                    if m:
                        # create log file
                        cl = m.group("cl")
                        func = m.group("func")
                        name = rserver + "/" + cl + "_" + func + ".log"
                        logfile = open(name, "w")
                        if not dbustests.get(cl):
                            dbustests[cl] = {}
                        if is_okay:
                            # okay: write a single line with the full test description
                            dbustests[cl][func] = "okay"
                            logfile.write(line)
                            logfile = None
                        else:
                            # failed: start writing lines into separate log file
                            dbustests[cl][func] = m.group("type")
                            sepcount = 0
                    if logfile:
                        logfile.write(line)
                        if line.startswith("-----------------------------------"):
                            sepcount = sepcount + 1
                            if sepcount >= 2:
                                # end of failure output for this test
                                logfile = None
                # now write XML
                indent +=space
                indents.append(indent)
                for testclass in dbustests:
                    result.write('%s<%s prefix="">\n' %
                                 (indent, testclass))
                    indent +=space
                    indents.append(indent)
                    for testfunc in dbustests[testclass]:
                        result.write('%s<%s>%s</%s>\n' %
                                     (indent, testfunc,
                                      dbustests[testclass][testfunc], 
                                      testfunc))
                    indents.pop()
                    indent = indents[-1]
                    result.write('%s</%s>\n' %
                                 (indent, testclass))
                indents.pop()
                indent = indents[-1]
            else:
                for log in logs:
                    if os.path.basename(log) in ['____compare.log',
                                                 'syncevo.log', # D-Bus server output
                                                 'dbus.log', # dbus-monitor output
                                                 ]:
                        continue
                    # <path>/Client_Sync_eds_contact_testItems.log
                    # <path>/SyncEvo_CmdlineTest_testConfigure.log
                    # <path>/N7SyncEvo11CmdlineTestE_testConfigure.log - C++ name mangling?
                    m = re.match(r'.*/(Client_Source_|Client_Sync_|N7SyncEvo\d+|[^_]*_)(.*)_([^_]*)', log)
                    # Client_Sync_, Client_Source_, SyncEvo_, ...
                    prefix = m.group(1)
                    # eds_contact, CmdlineTest, ...
                    format = m.group(2)
                    if(format not in logdic):
                        logdic[format]=[]
                    logdic[format].append(log)
                    logprefix[format]=prefix
            for format in logdic.keys():
                indent +=space
                indents.append(indent)
                prefix = logprefix[format]
                qformat = format;
                # avoid + sign in element name (not allowed by XML);
                # code reading XML must replace _- with + and __ with _
                qformat = qformat.replace("_", "__");
                qformat = qformat.replace("+", "_-");
                result.write(indent+'<'+qformat+' prefix="'+prefix+'">\n')
                for case in logdic[format]:
                    indent +=space
                    indents.append(indent)
                    casename = case.rpartition('_')[2].partition('.')[0]
                    result.write(indent+'<'+casename+'>')
                    match=format+'::'+casename
                    matchOk=match+": okay \*\*\*"
                    matchKnownFailure=match+": \*\*\* failure ignored \*\*\*"
                    if not os.system("grep -q '" + matchKnownFailure + "' "+case):
                       result.write('knownfailure')
                    elif not os.system("tail -10 %s | grep -q 'external transport failure (local, status 20043)'" % case):
                        result.write('network')
                    elif os.system("grep -q '" + matchOk + "' "+case):
                       result.write('failed')
                    else:
                        result.write('okay')
                    result.write('</'+casename+'>\n')
                    indents.pop()
                    indent = indents[-1]
                result.write(indent+'</'+qformat+'>\n')
                indents.pop()
                indent = indents[-1]
            result.write(indent+'</'+server+'>\n')
            indents.pop()
            indent = indents[-1]
            if(sourceServersRun == 0):
                #all source servers have been processed, end the source tag and
                #start the sync tags
                result.write(indent+'''</source>\n''')
                indents.pop()
                indent = indents[-1]
    if(haveSync):
        result.write(indent+'</sync>\n')
        indents.pop()
        indent=indents[-1]
    result.write(indent+'''</client-test>\n''')
    indents.pop()
    indents = indents[-1]

if(__name__ == "__main__"):
    if (len(sys.argv)!=7):
        print "usage: python resultchecker.py resultdir servers resulturi srcdir shellprefix backenddir"
    else:
        check(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])
