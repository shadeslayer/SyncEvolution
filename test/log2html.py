#!/usr/bin/python

"""
Converts the .log output for a client-test test into HTML, with
hightlighting and local hrefs to ClientTest.html and test directories.
"""

import sys
import re
import cgi

filename = sys.argv[1]
if filename == '-':
    log = sys.stdin
else:
    log = open(filename)

out = sys.stdout

# matches [DEBUG/DEVELOPER/...] tags at the start of the line,
# used to mark text via <span class=mode>
class ModeSpan:
    mode=None

    @staticmethod
    def setMode(newmode):
        if ModeSpan.mode != newmode:
            if ModeSpan.mode:
                out.write('</span>')
            ModeSpan.mode = newmode
            if ModeSpan.mode:
                out.write('<span class="%s">' % ModeSpan.mode)

# detect SyncEvolution line prefix
tag = re.compile(r'^(\[([A-Z]+) [^\]]*\])( .*)')

# hyperlink to HTML version of source code
sourcefile = re.compile(r'((\w+\.(?:cpp|c|h)):(\d+))')

# highlight:
# - any text after ***
#   *** clean via source A
#   *** starting Client::Source::egroupware-dav_caldav::testChanges ***
# - simple prefixes at the start of a line, after the [] tag (removed already)
#    caldav #A:
# - HTTP requests
#   REPORT /egw/groupdav.php/syncevolution/calendar/ HTTP/1.1
#   PROPFIND /egw/groupdav.php/syncevolution/calendar/1234567890%40dummy.ics HTTP/1.1
highlight = re.compile(r'(\*\*\* .*|^ [a-zA-Z0-9_\- #]*: |(?:REPORT|PUT|GET|DELETE|PROPFIND) .*HTTP/\d\.\d$)')

# hyperlink to sync session directory
session = re.compile(r'(Client_(?:Source|Sync)(?:_\w+)+\S+)')

out.write('''<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
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

def simplifyFilename(test):
    "same as client-test-main.cpp simplifyFilename()"
    test = test.replace(':', '_')
    test = test.replace('__', '_')
    return test

for line in log:
    line = line.rstrip()
    m = tag.match(line)
    if m:
        ModeSpan.setMode(m.group(2))
        out.write('<span class="tag">%s</span>' % m.group(1))
        line = m.group(3)
    line = cgi.escape(line)
    line = sourcefile.sub(r'<a href="\2.html#True-\3">\1</a>', line)
    line = highlight.sub(r'<span class="hl">\1</span>', line)
    line = session.sub(r'<a href="\1/">\1</a>', line)
    out.write(line)
    out.write('\n')

# close any <span class=mode> opened before
ModeSpan.setMode(None)

out.write('''
</pre>
</body>
</html>''')
