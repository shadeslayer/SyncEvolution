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

""" 
resultcheck.py: tranverse the test result directory, generate an XML
based test report.
"""

space="  "
def check (resultdir, serverlist,resulturi, srcdir, shellprefix, backenddir):
    '''Entrypoint, resutldir is the test result directory to be generated,
    resulturi is the http uri, it will only process corresponding server's
    test results list in severlist'''
    servers = serverlist.split(",")
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
    result.write('''</nightly-test>\n''')
    result.close()

def step1(input, result, indents, dir, resulturi, shellprefix, srcdir):
    '''Step1 of the result checking, collect system information and 
    check the preparation steps (fetch, compile)'''
    cont = True
    # get the chroot envoriment
    root = ''
    if(shellprefix.split()[0] == 'schroot'):
        root = shellprefix.split()[3]
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
    if(len(root) > 0):
        result.write(indent+'''<chrootinfo>\n''')
        fout,fin=popen2.popen2('schroot -i -c'+root+" |grep 'Description'")
        s = fout.read()
        s = s + ' name: '+root;
        result.write(indent+s)
        result.write(indent+'''</chrootinfo>\n''')
    result.write(indent+'''<libraryinfo>\n''')
    libs = ['libsoup-2.4', 'evolution-data-server-1.2', 'glib-2.0','dbus-glib-1']
    s=''
    oldpath = os.getcwd()
    os.chdir(srcdir)
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
    cmd='sed -n '
    for server in servers:
        cmd+= '-e /^'+server+'/p '
    fout,fin=popen2.popen2(cmd +resultdir+'/output.txt')
    params={}
    for line in fout:
        for server in servers:
            if(line.startswith(server) and server not in params):
                t = line.partition(server)[2].rpartition('\n')[0]
                if(t.startswith(':')):
                    t=t.partition(':')[2]
                params[server]=t
    '''generate a template which lists all test cases we supply, this helps 
    generate a comparable table and track potential uncontentional skipping
    of test cases'''
    templates=[]
    oldpath = os.getcwd()
    os.chdir (srcdir)
    print 'in client-test -h'
    fout,fin=popen2.popen2(shellprefix + " env LD_LIBRARY_PATH=build-synthesis/src/.libs SYNCEVOLUTION_BACKEND_DIR="+backenddir +" ./client-test -h |grep 'Client::Sync::vcard21'|grep -v 'Retry' |grep -v 'Suspend' | grep -v 'Resend'")
    sys.stdout.flush()
    os.chdir(oldpath)
    for line in fout:
        l = line.partition('Client::Sync::vcard21::')[2].rpartition('\n')[0]
        if(l!=''):
            templates.append(l);
    indent =indents[-1]+space
    indents.append(indent)
    '''start of testcase results '''
    result.write(indent+'''<client-test>\n''')
    runservers = os.listdir(resultdir)
    ''' This is a hack, because we have'syncevolution' and 'evolution' as
    test names, change the name to avoid miss match'''
    if 'evolution' in servers:
        servers.remove('evolution')
        servers.insert(0,'-evolution')
        params['source']=params['evolution']
    if 'synthesis' in servers:
        servers.remove('synthesis')
        servers.append('-synthesis')
    syncprinted = False;
    for server in servers:
        matched = False
	'''Only process servers listed in the input parametr'''
        for rserver in runservers:
            if(rserver.find(server) != -1):
                matched = True
                break;
        if(matched):
            indent +=space
            indents.append(indent)
            if(server == '-evolution'):
                server='source'
            elif (server == '-synthesis'):
                server='synthesis'
	        '''This is another hacking. Local source test is treated the same as 
	        server test while we want to treat them differenly in the test
	        report. Put all server test results under anohter tag 'sync' '''
            elif not syncprinted:
                syncprinted = True
                result.write(indent+'<sync>\n')
                indent +=space
                indents.append(indent)
                result.write(indent+'<template>')
                for template in templates:
                    result.write('<'+template+'/>')
                result.write('</template>\n')
            result.write(indent+'<'+server+' path="' +rserver+'" ')
            #valgrind check resutls
            outputlog = resultdir+'/'+rserver+'/output.txt'
            checkcmd = "grep 'valgrindcheck' %s |grep -o '\./client-test Client::Sync:.*$' " % (outputlog)
            fout,fin=popen2.popen2(checkcmd)
            s = fout.read()
            if(s):
                result.write('result="'+s.partition('return code ')[2].partition(')')[0]+'" ')
            result.write('parameter="'+params[server]+'">\n')
            logs = glob.glob(resultdir+'/'+rserver+'/*.log')
            logdic ={}
            logprefix ={}
            for log in logs:
                if(log.endswith('____compare.log')):
                    continue
                if(len(log.split('_')) > 2):
                    format = log.rpartition('_')[0].partition('_')[2].partition('_')[2]
                    prefix = log.rpartition(format)[0].rpartition('/')[-1]
                else:
                    format = log.partition('_')[0].rpartition('/')[-1]
                    prefix = ''
                if(format not in logdic):
                    logdic[format]=[]
                logdic[format].append(log)
                logprefix[format]=prefix
            for format in logdic.keys():
                indent +=space
                indents.append(indent)
                prefix = logprefix[format]
                result.write(indent+'<'+format+' prefix="'+prefix+'">\n')
                for case in logdic[format]:
                    indent +=space
                    indents.append(indent)
                    casename = case.rpartition('_')[2].partition('.')[0]
                    result.write(indent+'<'+casename+'>')
                    match=case.rpartition('/')[2].rpartition('.')[0].replace('_','::')
                    match=match+": \*\*\* fail"
                    if(os.system("grep -q '" + match + "' "+case)):
                       result.write('okay')
                    else:
                        result.write('failed')
                    result.write('</'+casename+'>\n')
                    indents.pop()
                    indent = indents[-1]
                result.write(indent+'</'+format+'>\n')
                indents.pop()
                indent = indents[-1]
            result.write(indent+'</'+server+'>\n')
            indents.pop()
            indent = indents[-1]
    if(syncprinted):
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
