<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" indent="no"/>

<xsl:template name="normalize-name">
	<xsl:param name="value"/>
	<xsl:variable name="normalized" select="concat( translate(substring(@name, 1, 1), '_abcdefghijklmnopqrstuvwrxyz', '-ABCDEFGHIJKLMNOPQRSTUVWRXYZ'), translate(substring(@name, 2), '_ABCDEFGHIJKLMNOPQRSTUVWRXYZ', '-abcdefghijklmnopqrstuvwrxyz'))"/>
	<xsl:choose>
		<xsl:when test="$normalized = 'Name'">name</xsl:when>
		<xsl:otherwise>rhcs<xsl:value-of select="$normalized"/></xsl:otherwise>
	</xsl:choose>
</xsl:template>

<xsl:template match="/resource-agent">
#
# Resource Agent: <xsl:value-of select="@name"/>
# Provider: <xsl:value-of select="@provider"/>
# Provider Version: <xsl:value-of select="@version"/>
# API Version: <xsl:value-of select="normalize-space(version)"/>
# Description: <xsl:value-of select="normalize-space(shortdesc)"/>
#
objectClasses: ( 
	1.3.6.1.4.1.2312.8.1.2.@@OBJ_CLASS_<xsl:call-template name="normalize-name"><xsl:with-param name="value" select="@name"/></xsl:call-template>@@ NAME '<xsl:call-template name="normalize-name">
		<xsl:with-param name="value" select="@name"/>
	</xsl:call-template>' SUP top STRUCTURAL
	DESC '<xsl:value-of select="normalize-space(shortdesc)"/>'
	MUST( <xsl:for-each select="parameters/parameter">
		<xsl:if test="@required = 1 or @primary = 1">
			<xsl:call-template name="normalize-name">
				<xsl:with-param name="value" select="@name"/>
			</xsl:call-template>
			<xsl:if test="position()!=last()"> $ </xsl:if>
		</xsl:if>
	</xsl:for-each> )
	MAY( <xsl:for-each select="parameters/parameter">
		<xsl:if test="string(number(@primary)) = 'NaN' or @primary = 0">
			<xsl:if test="string(number(@required)) = 'NaN' or @required = 0">
				<xsl:call-template name="normalize-name">
					<xsl:with-param name="value" select="@name"/>
				</xsl:call-template>
				<xsl:if test="position()!=last()"> $ </xsl:if>
			</xsl:if>
		</xsl:if>
	</xsl:for-each> )
)

#
# Object Attributes
# FIXME: Run a list of known attribute types
#
<xsl:for-each select="parameters/parameter">
<xsl:if test="@name != 'name'">
attributeTypes: (
	1.3.6.1.4.1.2312.8.1.1.@@ATTR_TYPE_<xsl:call-template name="normalize-name"><xsl:with-param name="value" select="@name"/></xsl:call-template>@@ NAME '<xsl:call-template name="normalize-name"><xsl:with-param name="value" select="@name"/></xsl:call-template>'
	DESC '<xsl:value-of select="normalize-space(shortdesc)"/>'
	<xsl:choose>
	<xsl:when test="content/@type = 'integer'">EQUALITY integerMatch
	SYNTAX 1.3.6.1.4.1.1466.115.121.1.27</xsl:when>
	<xsl:when test="content/@type = 'boolean'">EQUALITY booleanMatch
	SYNTAX 1.3.6.1.4.1.1466.115.121.1.7</xsl:when>
	<xsl:otherwise>EQUALITY caseExactIA5Match
	SYNTAX 1.3.6.1.4.1.1466.115.121.1.26</xsl:otherwise>
	</xsl:choose>
	SINGLE-VALUE
)
</xsl:if>
</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
