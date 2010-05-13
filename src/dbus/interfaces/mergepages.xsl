<?xml version='1.0'?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<!-- 
 * Copyright (C) 2010 Intel Corporation
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

<!--
   Merge many docbook reference pages into one single page
-->

<xsl:output method="xml" indent="yes" encoding="UTF-8"/>
<xsl:param name="file"/>
<xsl:variable name="filenode" select="document($file)"/>

<xsl:template match="/">
    <xsl:copy>
        <xsl:apply-templates/>
    </xsl:copy>
</xsl:template>

<xsl:template match="reference">
    <xsl:copy>
        <xsl:apply-templates/>
        <xsl:apply-templates select="$filenode/reference/refentry"/>
    </xsl:copy>
</xsl:template>

<xsl:template match="@*|node()|">
    <xsl:copy>
        <xsl:apply-templates select="@*|node()"/>
    </xsl:copy>
</xsl:template>
</xsl:stylesheet>
