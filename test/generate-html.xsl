<?xml version='1.0'?>
<!--
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
-->
<xsl:stylesheet version="1.0" 
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
    <!-- This program is used to generate html to represent nightly test results by combining today's test results and comparison info -->
    <!-- TODO: Add more checkings to avoid generate un-welformed html files -->
    <xsl:strip-space elements="*"/>
    <xsl:param name="cmp_result_file" select="''"/> <!-- comparsion result file, necessary -->
    <xsl:param name="url" select="''"/> <!-- root URL, at least pass . for relative URLs -->
	<xsl:output method="html" indent="yes" encoding="UTF-8"/>

    <!-- comparison result strings -->
    <xsl:variable name="equal" select="'equal'"/> 
    <xsl:variable name="equal-in-semantics" select="'equal-in-semantics'"/>
    <xsl:variable name="regression" select="'regression'"/>
    <xsl:variable name="improved" select="'improved'"/>
    <xsl:variable name="skipped" select="'skipped'"/>
    <xsl:variable name="invalid-value" select="'invalid-value'"/>

    <!-- log file suffix name -->
    <xsl:variable name="log-file-suffix" select="'.log.html'"/>
	
    <xsl:template match="/">
        <xsl:choose>
            <xsl:when test="$cmp_result_file!=''"> <!-- check comparison file path -->
                <html>
                    <xsl:call-template name="generate-html-header"/>
                    <xsl:variable name="cmp_result_tree" select="document($cmp_result_file)"/>
                    <xsl:call-template name="generate-html-body">
                        <xsl:with-param name="cmp_result_tree" select="$cmp_result_tree"/>
                    </xsl:call-template>
                </html>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text>No cmp_result_file, please set comparison result file by: 
                    --stringparam cmp_result_file [your file path]
                </xsl:text>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>
    <xsl:template name="generate-html-body">
        <xsl:param name="cmp_result_tree"/>
        <body>
            <!-- xsl:variable name="log-dir-uri" select="nightly-test/log-info/uri"/ -->
            <xsl:variable name="log-dir-uri" select="$url"/>
            <xsl:call-template name="generate-source-info">
                <xsl:with-param name="sourceinfo" select="nightly-test/source-info"/>
            </xsl:call-template>
            <xsl:call-template name="generate-platform-info">
                <xsl:with-param name="platform" select="nightly-test/platform-info"/>
            </xsl:call-template>
            <xsl:call-template name="generate-prepare-info">
                <xsl:with-param name="source" select="nightly-test/prepare"/>
                <xsl:with-param name="cmp-result" select="$cmp_result_tree/comparison/prepare"/>
                <xsl:with-param name="log-path" select="$log-dir-uri"/>
            </xsl:call-template>
            <!-- todo: check compile status, if it fails, no need to generate client-test info -->
            <xsl:call-template name="generate-client-test-info">
                <xsl:with-param name="source" select="nightly-test/client-test"/>
                <xsl:with-param name="cmp-result" select="$cmp_result_tree/comparison/client-test"/>
                <xsl:with-param name="log-path" select="$log-dir-uri"/>
            </xsl:call-template>
            <xsl:call-template name="generate-html-notes"/>
        </body>
    </xsl:template>

    <xsl:template name="generate-client-test-info">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->

        <h1><font color="Olive"> Client-test Results</font> </h1>
        <xsl:choose>
            <xsl:when test="$source">
                <h2> Client-test Summary </h2>
                <xsl:call-template name="generate-servers-summary">
                    <xsl:with-param name="unit-cases" select="$source/sync/template/*"/>
                    <xsl:with-param name="servers" select="$source/sync/*[name(.)!='template']"/>
                    <xsl:with-param name="cmp-result" select="$cmp-result/sync"/>
                    <xsl:with-param name="log-path" select="$log-path"/>
                </xsl:call-template>

                <xsl:call-template name="generate-source-summary">
                    <xsl:with-param name="source" select="$source/source"/>
                    <xsl:with-param name="cmp-result" select="$cmp-result/source"/>
                    <xsl:with-param name="log-path" select="$log-path"/>
                </xsl:call-template>

                <h2> Client-test Detail</h2>
                <xsl:call-template name="generate-client-test-sync">
                    <xsl:with-param name="source" select="$source/sync"/>
                    <xsl:with-param name="cmp-result" select="$cmp-result/sync"/>
                    <xsl:with-param name="log-path" select="$log-path"/>
                </xsl:call-template>

                <xsl:call-template name="generate-client-test-source">
                    <xsl:with-param name="source" select="$source/source"/>
                    <xsl:with-param name="cmp-result" select="$cmp-result/source"/>
                    <xsl:with-param name="log-path" select="$log-path"/>
                </xsl:call-template>
            </xsl:when>
            <xsl:otherwise>
                No client-test info!
                <br/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-client-test-source">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->

        <h3>Client::Source Test Results</h3>
        <xsl:for-each select="$source/*">
            <xsl:variable name="onesource" select="."/>
            <xsl:variable name="cmp-result" select="$cmp-result/*[name(.)=name($onesource)]"/>
            <h3>
                <a name="{name($onesource)}" href="{concat($log-path,'/',string($onesource/@path))}">
                    <xsl:value-of select="name($onesource)"/>
                </a>
            </h3>
            <xsl:call-template name="generate-desc-and-table">
                <xsl:with-param name="source" select="$onesource"/>
                <xsl:with-param name="cmp-result" select="$cmp-result"/>
                <xsl:with-param name="log-path" select="concat($log-path,'/',string($onesource/@path), '/')"/>
            </xsl:call-template>
        </xsl:for-each>
    </xsl:template>

    <xsl:template name="generate-client-test-sync">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->

        <xsl:variable name="list-of-unit-cases" select="$source/template/*"/>
        <xsl:variable name="servers" select="$source/*[name(.)!='template']"/>
        <xsl:variable name="target-servers" select="$cmp-result/*"/>

        <h3> Client::Sync Test Results </h3>
        <xsl:for-each select="$servers">
            <xsl:variable name="server" select="."/>
            <xsl:variable name="cmp-result" select="$cmp-result/*[name(.)=name($server)]"/>
            <xsl:call-template name="generate-server-table">
                <xsl:with-param name="source" select="$server"/>
                <xsl:with-param name="cmp-result" select="$cmp-result"/>
                <xsl:with-param name="log-path" select="concat($log-path,'/',$server/@path,'/')"/>
                <xsl:with-param name="desc-size" select="'2'"/>
                <xsl:with-param name="description" select="name($server)"/>
                <xsl:with-param name="list-of-unit-cases" select="$list-of-unit-cases"/>
            </xsl:call-template>
        </xsl:for-each>
    </xsl:template>
    
    <xsl:template name="generate-html-header">
        <head>
            <meta http-equiv="Content-Type" content="text/html; charset=utf-8"/>
            <title>SyncEvolution NightlyTest</title>
        </head>
    </xsl:template>

    <xsl:template name="generate-source-info">
        <xsl:param name="sourceinfo"/>
        <xsl:variable name="list" select="$sourceinfo/*"/>
        <xsl:comment>
            Generate source information in a table
        </xsl:comment>
        <h1><font color="Olive"> Source Information </font> </h1>
        <xsl:choose>
            <xsl:when test="count($list)">
              <xsl:for-each select="$sourceinfo/*">
                <h3><xsl:value-of select="./@name"/></h3>
                <pre><xsl:value-of select="./description"/></pre>
                <xsl:choose><xsl:when test="count(./patches/*)">Patches:<br/></xsl:when></xsl:choose>
                <xsl:for-each select="./patches/*">
                  <a href="{./path}"><xsl:value-of select="./summary"/></a><br/>
                </xsl:for-each>
              </xsl:for-each>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text> No source information!</xsl:text>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-platform-info">
        <xsl:param name="platform"/>
        <xsl:variable name="list" select="$platform/*"/>
        <xsl:comment>
            Generate platform information in a table
        </xsl:comment>
        <h1><font color="Olive"> Platform Information </font> </h1>
        <xsl:choose>
            <xsl:when test="count($list)">
                <table border="2">
                    <tr>
                        <th>Item</th>
                        <th>Value</th>
                    </tr>
                    <xsl:for-each select="$platform/*">
                        <tr>
                            <td><xsl:value-of select="name(.)"/></td>
                            <td><xsl:value-of select="."/></td>
                        </tr>
                    </xsl:for-each>
                </table>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text> No platform information!</xsl:text>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-prepare-info">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->

        <xsl:comment>
            Generate preparation information in a table
        </xsl:comment>
        <h1><font color="Olive"> Preparation Results </font> </h1>
        <xsl:call-template name="generate-table-info">
            <xsl:with-param name="source" select="$source"/>
            <xsl:with-param name="cmp-result" select="$cmp-result"/>
            <xsl:with-param name="log-path" select="concat($log-path,'/')"/>
            <xsl:with-param name="log-name" select="'output.txt'"/>
        </xsl:call-template>
    </xsl:template>
    <xsl:template name="check-prepare">
        <xsl:variable name="nodes-all" select="nightly-test/prepare/*"/>
        <xsl:if test="count($nodes-all)!=0 and contains(string($nodes-all),'failed')">
            <xsl:text>error</xsl:text>
        </xsl:if>
    </xsl:template>
    <xsl:template name="generate-servers-summary">
        <xsl:param name="unit-cases"/>
        <xsl:param name="servers"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/>

        <h3> Servers Interoperability Test Summary </h3>
        <table border="2">
            <tr>
                <th width="60">Server</th>
                <th width="60">Valgrind</th>
                <th width="60">Status</th>
                <th width="60">Categories</th>
                <th width="60">Total Cases</th>
                <th width="60">Passed</th>
                <th width="60">Known Failure</th>
                <th width="60">Network Failure</th>
                <th width="60">Failed</th>
                <th width="60">Skipped</th>
                <th width="60">Passrate</th>
                <th width="60">Improved</th>
                <th width="60">Regression</th>
            </tr>

            <xsl:for-each select="$servers">
                <xsl:variable name="item" select="."/>
                <xsl:variable name="categories" select="$item/*"/>
                <xsl:variable name="all" select="count($unit-cases) * count($categories)"/>
                <xsl:variable name="passed" select="count($item/*/*[.='okay'])"/>
                <xsl:variable name="knownfailure" select="count($item/*/*[.='knownfailure'])"/>
                <xsl:variable name="network" select="count($item/*/*[.='network'])"/>
                <xsl:variable name="failed" select="count($item/*/*[.='failed'])"/>
                <xsl:variable name="status" select="$cmp-result/*[name(.)=name($item)]/@summary"/>
                <xsl:variable name="real-regression" select="$cmp-result/*[name(.)=name($item)]/*/*[.=$regression]"/>
                <xsl:variable name="real-improved" select="$cmp-result/*[name(.)=name($item)]/*/*[.=$improved]"/>
                <xsl:variable name="valgrind" select="$item/@result"/>
                <xsl:variable name="valgrind-cmp" select="$cmp-result/*[name(.)=name($item)]/@result"/>
                <xsl:variable name="retcode" select="$item/@result"/>
                <xsl:variable name="retcode-cmp" select="$cmp-result/*[name(.)=name($item)]/@retcode"/>
                <xsl:variable name="path" select="concat($log-path,'/', $item/@path)"/>
                <xsl:variable name="servername">
                  <xsl:call-template name="stringescape">
                    <xsl:with-param name="string" select="name($item)"/>
                  </xsl:call-template>
                </xsl:variable>
                <tr>
                    <td> <!-- server name -->
                        <a href="#{$servername}">
                            <xsl:value-of select="$servername"/>
                        </a>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="$valgrind=100">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$valgrind-cmp"/>
                                        <xsl:with-param name="source" select="'failed'"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <a href="{$path}/output.txt">
                                    failed
                                </a>
                            </xsl:when>
                            <xsl:otherwise>ok</xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="not($retcode) or $retcode=0">
                                ok
                            </xsl:when>
                            <xsl:otherwise>
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$retcode-cmp"/>
                                        <xsl:with-param name="source" select="'failed'"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <a href="{$path}/output.txt">
                                    failed
                                </a>
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td>
                        <xsl:for-each select="$categories">
                            <xsl:call-template name="stringescape">
                                <xsl:with-param name="string" select="name(.)"/>
                            </xsl:call-template>
                            <xsl:if test="position()!=last()">
                                <xsl:value-of select="', '"/>
                            </xsl:if>
                        </xsl:for-each>
                    </td>
                    <td>
                        <xsl:value-of select="$all"/>
                    </td>
                    <td>
                        <xsl:value-of select="$passed"/>
                    </td>
                    <td>
                        <xsl:value-of select="$knownfailure"/>
                    </td>
                    <td>
                        <xsl:value-of select="$network"/>
                    </td>
                    <td>
                        <xsl:value-of select="$failed"/>
                    </td>
                    <td>
                        <xsl:value-of select="$all - $passed - $failed"/>
                    </td>
                    <td>
                        <xsl:variable name="passrate" select="$passed div ($passed+$failed) * 100"/>
                        <!--xsl:value-of select="concat(substring-before(string($passrate),'.'),'.', su)"/-->
                        <xsl:value-of select="substring(string($passrate),1,5)"/>
                        <xsl:value-of select="'%'"/>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="count($real-improved)">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$improved"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <xsl:value-of select="count($real-improved)"/>
                            </xsl:when>
                            <xsl:otherwise>
                                0
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="count($real-regression)">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$regression"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <xsl:value-of select="count($real-regression)"/>
                            </xsl:when>
                            <xsl:otherwise>
                                0
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                </tr>
            </xsl:for-each>

        </table>
    </xsl:template>

    <xsl:template name="count-all-cases">
        <xsl:param name="source"/>
        <xsl:call-template name="count-biggest-cases">
            <xsl:with-param name="source" select="$source/*[position()=1]"/>
        </xsl:call-template>
    </xsl:template>

    <xsl:template name="count-biggest-cases">
        <xsl:param name="source"/>
        <xsl:choose>
            <xsl:when test="$source">
                <xsl:variable name="current">
                    <xsl:call-template name="count-one-source-cases">
                        <xsl:with-param name="source" select="$source/*[position()=1]"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:variable name="next-ones">
                    <xsl:call-template name="count-biggest-cases">
                        <xsl:with-param name="source" select="$source/following-sibling::*[position()=1]"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:choose>
                    <xsl:when test="number(current) &lt; number(next-ones)">
                        <xsl:value-of select="$next-ones"/>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:value-of select="$current"/>
                    </xsl:otherwise>
                </xsl:choose>
            </xsl:when>
            <xsl:otherwise>0</xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="count-one-source-cases">
        <xsl:param name="source"/>
        <xsl:choose>
            <xsl:when test="$source">
                <xsl:variable name="current" select="count($source/*)"/>
                <xsl:variable name="next-ones">
                    <xsl:call-template name="count-one-source-cases">
                        <xsl:with-param name="source" select="$source/following-sibling::*[position()=1]"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:value-of select="$current+number($next-ones)"/>
            </xsl:when>
            <xsl:otherwise>0</xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-source-summary">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/>

        <h3> Client Source Test Summary </h3>
        <table border="2">
            <tr>
                <th width="60">Sources</th>
                <th width="60">Valgrind</th>
                <th width="60">Status</th>
                <th width="60">Total Cases</th>
                <th width="60">Passed</th>
                <th width="60">Known Failure</th>
                <th width="60">Network Failure</th>
                <th width="60">Failed</th>
                <th width="60">Skipped</th>
                <th width="60">Passrate</th>
                <th width="60">Improved</th>
                <th width="60">Regression</th>
            </tr>
            <xsl:variable name="total">
                <xsl:call-template name="count-all-cases">
                    <xsl:with-param name="source" select="$source"/>
                </xsl:call-template>
            </xsl:variable>

            <xsl:for-each select="$source/*">
                <xsl:variable name="item" select="."/>
                <xsl:variable name="all" select="count($item/*)"/>
                <xsl:variable name="passed" select="count($item/*/*[.='okay'])"/>
                <xsl:variable name="knownfailure" select="count($item/*/*[.='knownfailure'])"/>
                <xsl:variable name="network" select="count($item/*/*[.='network'])"/>
                <xsl:variable name="failed" select="count($item/*/*[.='failed'])"/>
                <xsl:variable name="status" select="$cmp-result/*[name(.)=name($item)]/@summary"/>
                <xsl:variable name="valgrind" select="$item/@result"/>
                <xsl:variable name="valgrind-cmp" select="$cmp-result/*[name(.)=name($item)]/@result"/>
                <xsl:variable name="retcode" select="$item/@result"/>
                <xsl:variable name="retcode-cmp" select="$cmp-result/*[name(.)=name($item)]/@retcode"/>
                <xsl:variable name="path" select="concat($log-path, '/', $item/@path)"/>
                <xsl:variable name="servername">
                  <xsl:call-template name="stringescape">
                    <xsl:with-param name="string" select="name($item)"/>
                  </xsl:call-template>
                </xsl:variable>
                <tr>
                    <td> <!-- server name -->
                        <a href="#{$servername}">
                          <xsl:value-of select="$servername"/>
                        </a>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="$valgrind=100">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$valgrind-cmp"/>
                                        <xsl:with-param name="source" select="'failed'"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <a href="{$path}/output.txt">
                                    failed
                                </a>
                            </xsl:when>
                            <xsl:otherwise>ok</xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="not($retcode) or $retcode=0">
                                ok
                            </xsl:when>
                            <xsl:otherwise>
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$retcode-cmp"/>
                                        <xsl:with-param name="source" select="'failed'"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <a href="{$path}/output.txt">
                                    failed
                                </a>
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td> 
                        <xsl:value-of select="$total"/>
                    </td>
                    <td> 
                        <xsl:value-of select="$passed"/>
                    </td>
                    <td> 
                        <xsl:value-of select="$knownfailure"/>
                    </td>
                    <td>
                        <xsl:value-of select="$network"/>
                    </td>
                    <td> 
                        <xsl:value-of select="$total - $passed"/>
                    </td>
                    <td>
                        <!--xsl:value-of select="$total - $passed - $failed"/-->
                        0
                    </td>
                    <td>
                        <xsl:variable name="passrate" select="$passed div ($total) * 100"/>
                        <!--xsl:value-of select="concat(substring-before(string($passrate),'.'),'.', su)"/-->
                        <xsl:value-of select="substring(string($passrate),1,5)"/>
                        <xsl:value-of select="'%'"/>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="number($status) &gt; 0">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$improved"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <xsl:value-of select="$status"/>
                            </xsl:when>
                            <xsl:otherwise>
                                0
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                    <td>
                        <xsl:choose>
                            <xsl:when test="number($status) &lt; 0">
                                <xsl:attribute name="bgcolor">
                                    <xsl:call-template name="generate-color">
                                        <xsl:with-param name="status" select="$regression"/>
                                    </xsl:call-template>
                                </xsl:attribute>
                                <xsl:value-of select="0 - number($status)"/>
                            </xsl:when>
                            <xsl:otherwise>
                                0
                            </xsl:otherwise>
                        </xsl:choose>
                    </td>
                </tr>
            </xsl:for-each>
        </table>
    </xsl:template>

    <!-- generate each server interoperability test -->
    <xsl:template name="generate-server-table">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->
        <xsl:param name="desc-size" select="'3'"/>
        <xsl:param name="description" select="''"/>
        <xsl:param name="list-of-unit-cases"/>

        <xsl:comment>
            This is used to generate a table for each server test results for all PIMs
        </xsl:comment>
        <xsl:element name="h{$desc-size}"> 
            <a name="{name($source)}" href="{$log-path}">
                <xsl:value-of select="$description"/> 
            </a>
        </xsl:element>
        <xsl:variable name="type-list" select="$source/*[not(contains(name(.),'Retry')) and not(contains(name(.),'Suspend'))]"/>

        <table border="2" cellspacing="1">
            <tr>
                <th>Item</th>
                <xsl:for-each select="$type-list">
                    <th width="20">
                        <xsl:call-template name="stringescape">
                            <xsl:with-param name="string" select="name(.)"/>
                        </xsl:call-template>
                    </th>
                </xsl:for-each>
            </tr>

            <xsl:for-each select="$list-of-unit-cases">
                <!--xsl:sort select="name(.)" type="text"/ -->
                <xsl:variable name="unit" select="."/>
                <xsl:variable name="escapedunit">
                  <xsl:call-template name="stringescape">
                    <xsl:with-param name="string" select="name($unit)"/>
                  </xsl:call-template>
                </xsl:variable>
                <tr>
                    <td width="300">
                        <xsl:value-of select="$escapedunit"/>
                    </td>
                    <xsl:for-each select="$type-list">
                        <xsl:variable name="type" select="."/>
                        <xsl:variable name="status" select="$type/*[name(.)=name($unit)]"/>
                        <td width="20">
                            <xsl:variable name="color">
                                <xsl:variable name="item" select="$cmp-result/*[name(.)=name($type)]"/>
                                <xsl:call-template name="generate-color-with-list">
                                    <xsl:with-param name="unit" select="$status"/>
                                    <xsl:with-param name="cmp-result" select="$item"/>
                                </xsl:call-template>
                            </xsl:variable>
                            <xsl:if test="$color!=''">
                                <xsl:attribute name="bgcolor">
                                    <xsl:value-of select="$color"/>
                                </xsl:attribute>
                            </xsl:if>
                            <xsl:choose>
                                <xsl:when test="string($status)=''">
                                    skipped
                                </xsl:when>
                                <xsl:otherwise>
                                    <xsl:variable name='escapedtype'>
                                        <xsl:call-template name="stringescape">
                                            <xsl:with-param name="string" select="name($type)"/>
                                        </xsl:call-template>
                                    </xsl:variable>
                                    <a href="{concat($log-path,string(@prefix),$escapedtype,'_',$escapedunit,$log-file-suffix)}">
                                        <xsl:value-of select="$status"/>
                                    </a>
                                </xsl:otherwise>
                            </xsl:choose>
                        </td>
                    </xsl:for-each>
                </tr>
            </xsl:for-each>
            <!-- statistics -->
            <tr>
                <th>Total passed cases (all: <xsl:value-of select="count($list-of-unit-cases)"/>) </th>
                <xsl:for-each select="$type-list">
                    <xsl:variable name="type" select="."/>
                    <xsl:variable name="okays" select="$type/*[.='okay']"/>
                    <xsl:variable name="faileds" select="$type/*[.='failed']"/>
                    <th>
                        <xsl:variable name="color">
                            <xsl:variable name="item" select="$cmp-result/*[name(.)=name($type)]"/>
                            <xsl:call-template name="generate-color">
                                <xsl:with-param name="status" select="$item/@total-status"/>
                            </xsl:call-template>
                        </xsl:variable>
                        <xsl:if test="$color!=''">
                            <xsl:attribute name="bgcolor">
                                <xsl:value-of select="$color"/>
                            </xsl:attribute>
                        </xsl:if>

                        <!--table>
                            <tr><td>okay:   <xsl:value-of select="count($okays)"/></td></tr>
                            <tr><td>failed: <xsl:value-of select="count($faileds)"/></td></tr>
                            <tr><td>skipped:<xsl:value-of select="count($list-of-unit-cases)-count($okays)- count($faileds)"/></td></tr>
                        </table-->
                        <xsl:value-of select="count($okays)"/>
                    </th>
                </xsl:for-each>
            </tr>
        </table>
    </xsl:template>

    <xsl:template name="generate-desc-and-table">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/> <!-- log file path -->
        <xsl:variable name="list" select="$source/*"/>

        <xsl:choose>
            <xsl:when test="count($list)">
                <table>
                    <xsl:for-each select="$list">
                        <xsl:sort select="count(*)"/>
                        <xsl:variable name="item" select="."/>
                        <xsl:variable name="cmp-item" select="$cmp-result/*[name(.)=name($item)]"/>
                        <xsl:if test="(position() mod 3)=1">
                            <xsl:text disable-output-escaping="yes">&lt;tr&gt;</xsl:text>
                        </xsl:if>
                        <td>
                            <xsl:variable name="itemname">
                              <xsl:call-template name="stringescape">
                                <xsl:with-param name="string" select="name($item)"/>
                              </xsl:call-template>
                            </xsl:variable>
                            <xsl:call-template name="generate-table-info">
                                <xsl:with-param name="source" select="$item"/>
                                <xsl:with-param name="cmp-result" select="$cmp-item"/>
                                <xsl:with-param name="log-path" select="concat($log-path,$item/@prefix,$itemname)"/>
                                <xsl:with-param name="description" select="$itemname"/>
                            </xsl:call-template>
                        </td>
                        <xsl:if test="(position() mod 3)=0">
                            <xsl:text disable-output-escaping="yes">&lt;/tr&gt;</xsl:text>
                        </xsl:if>
                    </xsl:for-each>
                </table>
            </xsl:when>
            <xsl:otherwise>
                No results!
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-table-info">
        <xsl:param name="source"/>
        <xsl:param name="cmp-result"/>
        <xsl:param name="log-path"/>
        <xsl:param name="log-name" select="''"/>
        <xsl:param name="desc-size" select="'4'"/>
        <xsl:param name="description"/>
        <xsl:param name="desc-color" select="'black'"/>

        <xsl:comment>
            Generate a table for a list of unit test cases
        </xsl:comment>
        <xsl:variable name="list" select="$source/*"/>
        <xsl:choose>
            <xsl:when test="count($list)">
                <table border="2">
                    <xsl:variable name="sourcename">
                      <xsl:call-template name="stringescape">
                        <xsl:with-param name="string" select="name($source)"/>
                      </xsl:call-template>
                    </xsl:variable>

                    <tr>
                        <th><xsl:value-of select="$sourcename"/></th>
                        <th>Value</th>
                    </tr>
                    <xsl:for-each select="$source/*">
                        <!-- do not sort here: unit tests are run in a certain order which may matter -->
                        <!-- xsl:sort select="name(.)" data-type="text"/ -->
                        <xsl:variable name="testname">
                          <xsl:call-template name="stringescape">
                            <xsl:with-param name="string" select="name(.)"/>
                          </xsl:call-template>
                        </xsl:variable>
                        <tr>
                            <td width="300"><xsl:value-of select="$testname"/></td>
                            <td width="20">
                                <xsl:variable name="color">
                                    <xsl:call-template name="generate-color-with-list">
                                        <xsl:with-param name="unit" select="."/>
                                        <xsl:with-param name="cmp-result" select="$cmp-result"/>
                                    </xsl:call-template>
                                </xsl:variable>
                                <xsl:if test="$color!=''">
                                    <xsl:attribute name="bgcolor">
                                        <xsl:value-of select="$color"/>
                                    </xsl:attribute>
                                </xsl:if>
                                <xsl:choose>
                                    <xsl:when test=".='skipped'">
                                        <xsl:value-of select="."/>
                                    </xsl:when>
                                    <xsl:when test="$log-name!='' and @path">
                                        <a href="{concat($log-path,string(@path),'/',$log-name)}">
                                            <xsl:value-of select="."/>
                                        </a>
                                    </xsl:when>
                                    <xsl:when test="@path">
                                        <a href="{concat($log-path,string(@path),'/',$testname,$log-file-suffix)}">
                                            <xsl:value-of select="."/>
                                        </a>
                                    </xsl:when>
                                    <xsl:otherwise>
                                        <a href="{$log-path}_{concat($testname,$log-file-suffix)}">
                                            <xsl:value-of select="."/>
                                        </a>
                                    </xsl:otherwise>
                                </xsl:choose>

                            </td>
                        </tr>
                    </xsl:for-each>
                    <tr>
                        <th>Total passed cases (all: <xsl:value-of select="count($source/*)"/>) </th>
                        <th>
                            <xsl:variable name="okays" select="$source/*[.='okay']"/>
                            <xsl:variable name="color">
                                        <xsl:call-template name="generate-color">
                                            <xsl:with-param name="status" select="$cmp-result/@total-status"/>
                                        </xsl:call-template>
                            </xsl:variable>
                            <xsl:if test="$color!=''">
                                <xsl:attribute name="bgcolor">
                                    <xsl:value-of select="$color"/>
                                </xsl:attribute>
                            </xsl:if>
                            <xsl:value-of select="count($okays)"/>
                        </th>
                    </tr>
                </table>
            </xsl:when>
            <xsl:otherwise>No <xsl:value-of select="$description"/> information!
                <br/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>
    <xsl:template name="generate-color-with-list">
        <xsl:param name="unit"/>
        <xsl:param name="cmp-result"/>
        <xsl:for-each select="$cmp-result/*">
            <xsl:if test="name($unit)=name(.)">
                <xsl:call-template name="generate-color">
                    <xsl:with-param name="status" select="."/>
                    <xsl:with-param name="source" select="$unit"/>
                </xsl:call-template>
            </xsl:if>
        </xsl:for-each>
    </xsl:template>
    <xsl:template name="generate-color">
        <xsl:param name="status"/>
        <xsl:param name="source"/>
        <xsl:choose>
            <xsl:when test="string($status)=$improved">
                <xsl:text>green</xsl:text>
            </xsl:when>
            <xsl:when test="string($status)=$regression">
                <xsl:text>red</xsl:text>
            </xsl:when>
            <xsl:when test="(string($status)=$equal or string($status)=$equal-in-semantics)and $source='failed'">
                <xsl:text>gray</xsl:text>
            </xsl:when>
            <xsl:otherwise/>
        </xsl:choose>
    </xsl:template>

    <xsl:template name="generate-html-notes">
        <h3>Notes:</h3>
        <font color="red">Red</font>: regression 
        <font color="green">Green</font>: improvement 
        <font color="gray">Gray</font>: failed but not regression
        <br/>
    </xsl:template>

    <xsl:template name="stringescape">
        <xsl:param name="string"/>
        <xsl:variable name="str">
            <xsl:call-template name="stringreplaceall">
                <xsl:with-param name="orginalstring" select="$string"/>
                <xsl:with-param name="old" select="'__'"/>
                <xsl:with-param name="new" select="'_'"/>
            </xsl:call-template>
        </xsl:variable>
        <xsl:call-template name="stringreplaceall">
            <xsl:with-param name="orginalstring" select="string($str)"/>
            <xsl:with-param name="old" select="'_-'"/>
            <xsl:with-param name="new" select="'+'"/>
        </xsl:call-template>
    </xsl:template>

    <xsl:template name="stringreplaceall">
        <xsl:param name="orginalstring"/>
        <xsl:param name="old"/>
        <xsl:param name="new"/>
        <xsl:choose>
            <xsl:when test="contains($orginalstring, $old)">
                <xsl:value-of select="substring-before($orginalstring, $old)"/>
                <xsl:value-of select="$new"/>
                <xsl:call-template name="stringreplaceall">
                    <xsl:with-param name="orginalstring" select="substring-after($orginalstring, $old)"/>
                    <xsl:with-param name="old" select="$old"/>
                    <xsl:with-param name="new" select="$new"/>
                </xsl:call-template>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="$orginalstring"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:template>

</xsl:stylesheet>

