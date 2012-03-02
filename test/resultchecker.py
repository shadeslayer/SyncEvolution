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
import fnmatch
import cgi

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
        indents,cont = step1(resultdir,result,indents,resultdir,resulturi, shellprefix, srcdir)
        if (cont):
            step2(resultdir,result,servers,indents,srcdir,shellprefix,backenddir)
        else:
            # compare.xsl fails if there is no <client-test> element:
            # add an empty one
            result.write('''<client-test/>\n''')
    result.write('''</nightly-test>\n''')
    result.close()

patchsummary = re.compile('^Subject: (?:\[PATCH.*?\] )?(.*)\n')
patchauthor = re.compile('^From: (.*?) <.*>\n')
def extractPatchSummary(patchfile):
    author = ""
    for line in open(patchfile):
        m = patchauthor.match(line)
        if m:
            author = m.group(1) + " - "
        else:
            m = patchsummary.match(line)
            if m:
                return author + m.group(1)
    return os.path.basename(patchfile)

def step1(resultdir, result, indents, dir, resulturi, shellprefix, srcdir):
    '''Step1 of the result checking, collect system information and 
    check the preparation steps (fetch, compile)'''
    cont = True
    input = os.path.join(resultdir, "output.txt")
    indent =indents[-1]+space
    indents.append(indent)

    # include information prepared by GitCopy in runtests.py
    result.write(indent+'<source-info>\n')
    files = os.listdir(resultdir)
    files.sort()
    for source in files:
        m = re.match('(.*)-source.log', source)
        if m:
            name = m.group(1)
            result.write('   <source name="%s"><description><![CDATA[%s]]></description>\n' %
                         (name, open(os.path.join(resultdir, source)).read()))
            result.write('       <patches>\n')
            for patch in files:
                if fnmatch.fnmatch(patch, name + '-*.patch'):
                    result.write('          <patch><path>%s</path><summary><![CDATA[%s]]></summary></patch>\n' %
                                 ( patch, extractPatchSummary(os.path.join(resultdir, patch)) ) )
            result.write('       </patches>\n')
            result.write('   </source>\n')
    result.write(indent+'</source-info>\n')

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
                     'egroupware-dav',
                     'oracle',
                     'exchange',
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
                    cmd = shellprefix + " env LD_LIBRARY_PATH=build-synthesis/src/.libs SYNCEVOLUTION_BACKEND_DIR="+backenddir +" CLIENT_TEST_PEER_CAN_RESTART=1 CLIENT_TEST_SOURCES="+source+" ./client-test -h"
                    fout,fin=popen2.popen2(cmd)
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
                #
                # More recent Python 2.7 produces:
                # FAIL: testSyncSecondSession (__main__.TestSessionAPIsReal)

                # first build list of all tests, assuming that they pass
                dbustests = {}
                test_start = re.compile(r'''^Test(?P<cl>.*)\.test(?P<func>[^ ]*).*ok(?:ay)?$''')
                # FAIL/ERROR + description of test (old Python)
                test_fail = re.compile(r'''(?P<type>FAIL|ERROR): Test(?P<cl>.*)\.test(?P<func>[^ ]*)''')
                # FAIL/ERROR + function name of test (Python 2.7)
                test_fail_27 = re.compile(r'''(?P<type>FAIL|ERROR): test(?P<func>[^ ]*) \(.*\.(?:Test(?P<cl>.*))\)''')
                name = None
                logfile = None
                htmlfile = None
                linetype = None
                for line in open(rserver + "/output.txt"):
                    m = test_start.search(line)
                    if m:
                        is_okay = True
                    else:
                        m = test_fail.search(line) or test_fail_27.search(line)
                        is_okay = False
                    if m:
                        # create new (?!) log file
                        cl = m.group("cl")
                        func = m.group("func")
                        newname = rserver + "/" + cl + "_" + func + ".log"
                        if newname != name:
                            name = newname
                            logfile = open(name, "w")
                            if htmlfile:
                                htmlfile.write('</pre></body')
                                htmlfile.close()
                                htmlfile = None
                            htmlfile = open(name + ".html", "w")
                            htmlfile.write('''<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
   <meta http-equiv="content-type" content="text/html; charset=None">
   <style type="text/css">
td.linenos { background-color: #f0f0f0; padding-right: 10px; }
span.lineno { background-color: #f0f0f0; padding: 0 5px 0 5px; }
pre { line-height: 125%; }
body .hll { background-color: #ffffcc }
body  { background: #f8f8f8; }
span.INFO { background: #c0c0c0 }
span.ERROR { background: #e0c0c0 }
span.hl { color: #c02020 }
   </style>
</head>
<body>
<pre>''')
                            if not dbustests.get(cl):
                                dbustests[cl] = {}
                            if is_okay:
                                # okay: write a single line with the full test description
                                dbustests[cl][func] = "okay"
                                logfile.write(line)
                                logfile = None
                                htmlfile.write('<span class="OKAY">%s</span></pre></body>' % cgi.escape(line))
                                htmlfile.close()
                                htmlfile = None
                            else:
                                # failed: start writing lines into separate log file
                                dbustests[cl][func] = m.group("type")
                                linetype = "ERROR"
                                htmlfile.write('<a href="#dbus-traffic">D-Bus traffic</a> <a href="#stdout">output</a>\n\n')

                    if logfile:
                        logfile.write(line)
                        if line == 'D-Bus traffic:\n':
                            linetype = "DBUS"
                            htmlfile.write('<h3 id="dbus-traffic">D-Bus traffic:</h3>')
                        elif line == 'server output:\n':
                            linetype = "OUT"
                            htmlfile.write('<h3 id="stdout">server output:</h3>')
                        else:
                            htmlfile.write('<span class="%s">%s</span>' % (linetype, cgi.escape(line)))

                if htmlfile:
                    htmlfile.write('</pre></body')
                    htmlfile.close()
                    htmlfile = None

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
                    logname = os.path.basename(log)
                    if logname in ['____compare.log',
                                   'syncevo.log', # D-Bus server output
                                   'dbus.log', # dbus-monitor output
                                   ]:
                        continue
                    # <path>/Client_Sync_eds_contact_testItems.log
                    # <path>/SyncEvo_CmdlineTest_testConfigure.log
                    # <path>/N7SyncEvo11CmdlineTestE_testConfigure.log - C++ name mangling?
                    m = re.match(r'^(Client_Source_|Client_Sync_|N7SyncEvo\d+|[^_]*_)(.*)_([^_]*)\.log', logname)
                    if not m:
                        print "skipping", logname
                        continue
                    # Client_Sync_, Client_Source_, SyncEvo_, ...
                    prefix = m.group(1)
                    # eds_contact, CmdlineTest, ...
                    format = m.group(2)
                    # testImport
                    casename = m.group(3)
                    # special case grouping of some tests: include group inside casename instead of
                    # format, example:
                    # <path>/Client_Source_apple_caldav_LinkedItemsDefault_testLinkedItemsParent
                    m = re.match(r'(.*)_(LinkedItems\w+)', format)
                    if m:
                        format = m.group(1)
                        casename = m.group(2) + '::' + casename
                    print "analyzing log %s: prefix %s, subset %s, testcase %s" % (logname, prefix, format, casename)
                    if(format not in logdic):
                        logdic[format]=[]
                    logdic[format].append((casename, log))
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
                for casename, log in logdic[format]:
                    indent +=space
                    indents.append(indent)
                    # must avoid :: in XML
                    tag = casename.replace('::', '__')
                    result.write(indent+'<'+tag+'>')
                    match=format+'::'+casename
                    matchOk=match+": okay \*\*\*"
                    matchKnownFailure=match+": \*\*\* failure ignored \*\*\*"
                    if not os.system("grep -q '" + matchKnownFailure + "' "+log):
                       result.write('knownfailure')
                    elif not os.system("tail -10 %s | grep -q 'external transport failure (local, status 20043)'" % log):
                        result.write('network')
                    elif os.system("grep -q '" + matchOk + "' "+log):
                       result.write('failed')
                    else:
                        result.write('okay')
                    result.write('</'+tag+'>\n')
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
