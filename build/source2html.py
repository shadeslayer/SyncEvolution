#!/usr/bin/python

"""
Converts source code (first parameter, can be - for stdin) to HTML
(stdout), using pygments if installed, otherwise simple text
manipulation without syntax highlighting. In both cases the output
will have "True-<number>" as anchors for each line.
"""

import sys

filename = sys.argv[1]
if filename == '-':
    code = sys.stdin.read()
else:
    code = open(filename).read()

out = sys.stdout

try:
    import pygments
    import pygments.lexers
    from pygments.formatters import HtmlFormatter
    if filename == '-':
        lexer = pygments.lexers.guess_lexer(code)
    else:
        lexer = pygments.lexers.guess_lexer_for_filename(filename, code)
    formatter = HtmlFormatter(full=True, linenos=True, lineanchors=True)
    pygments.highlight(code, lexer, formatter, out)
except:
    import cgi
    print >>sys.stderr, "source2html.py failed with pygments:", sys.exc_info()
    print >>sys.stderr, "falling back to internal code"

    out.write('''<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01//EN" "http://www.w3.org/TR/html4/strict.dtd">
<html>
<head>
   <meta http-equiv="content-type" content="text/html; charset=None">
   <style type="text/css">
td.linenos { background-color: #f0f0f0; padding-right: 10px; }
span.lineno { background-color: #f0f0f0; padding: 0 5px 0 5px; }
pre { line-height: 125%; }
body .hll { background-color: #ffffcc }
body .hll { background-color: #ffffcc }
body  { background: #f8f8f8; }
   </style>
</head>
<body>
<table class="highlighttable"><tr><td class="linenos"><div class="linenodiv"><pre>''')
    lines = code.split('\n')
    for line in range(1, len(lines)):
        out.write('%4d\n' % line)
    out.write('%4d' % len(lines))
    out.write('</pre></div></td><td class="code"><div class="highlight"><pre>')
    for lineno, line in enumerate(lines):
        out.write('<a name="True-%d"></a>%s\n' % (lineno + 1, cgi.escape(line)))
    out.write('''</pre></div>
</td></tr></table></body>
</html>''')
