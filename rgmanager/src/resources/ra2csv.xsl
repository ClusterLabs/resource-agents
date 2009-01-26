<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" indent="no"/>

<xsl:template name="normalize-name">
	<xsl:param name="type"/>
	<xsl:param name="value"/>
	<xsl:variable name="normalized" select="concat( translate(substring(@name, 1, 1), '_abcdefghijklmnopqrstuvwrxyz', '-ABCDEFGHIJKLMNOPQRSTUVWRXYZ'), translate(substring(@name, 2), '_ABCDEFGHIJKLMNOPQRSTUVWRXYZ', '-abcdefghijklmnopqrstuvwrxyz'))"/>
	<xsl:choose>
		<xsl:when test="$normalized = 'Name'"></xsl:when>
		<xsl:otherwise><xsl:value-of select="$type"/>,rhcs<xsl:value-of select="$normalized"/><xsl:text>
</xsl:text></xsl:otherwise>
	</xsl:choose>
</xsl:template>

<xsl:template match="/resource-agent"><xsl:call-template name="normalize-name"><xsl:with-param name="type">obj</xsl:with-param><xsl:with-param name="value" select="normalize-space(@name)"/></xsl:call-template>
<xsl:for-each select="parameters/parameter"><xsl:call-template name="normalize-name"><xsl:with-param name="type">attr</xsl:with-param><xsl:with-param name="value" select="normalize-space(@name)"/></xsl:call-template>
</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
