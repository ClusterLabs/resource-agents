<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" indent="yes"/>
<xsl:template name="capitalize">
	<xsl:param name="value"/>
	<xsl:variable name="normalized" select="translate($value, '_abcdefghijklmnopqrstuvwrxyz', '-ABCDEFGHIJKLMNOPQRSTUVWRXYZ')"/>
	<xsl:value-of select="$normalized"/>
</xsl:template>

<xsl:template match="/resource-agent">
  &lt;define name="<xsl:call-template name="capitalize"><xsl:with-param name="value" select="@name"/></xsl:call-template>"&gt;
    &lt;element name="<xsl:value-of select="@name"/>" rha:description="<xsl:value-of select="normalize-space(shortdesc)"/>"&gt;
      &lt;choice&gt;
      &lt;group&gt;
        &lt;!-- rgmanager specific stuff --&gt;
        &lt;attribute name="ref" rha:description="Reference to existing <xsl:value-of select="@name"/> resource in the resources section."/&gt;
      &lt;/group&gt;
      &lt;group&gt;<xsl:for-each select="parameters/parameter">
		<xsl:choose>
			<xsl:when test="@required = 1 or @primary = 1">
        &lt;attribute name="<xsl:value-of select="@name"/>" rha:description="<xsl:value-of select="normalize-space(shortdesc)"/>"/&gt;</xsl:when>
			<xsl:otherwise>
        &lt;optional&gt;
          &lt;attribute name="<xsl:value-of select="@name"/>" rha:description="<xsl:value-of select="normalize-space(shortdesc)"/>"/&gt;
        &lt;/optional&gt;</xsl:otherwise>
		</xsl:choose>
	</xsl:for-each>
      &lt;/group&gt;
      &lt;/choice&gt;
      &lt;optional&gt;
        &lt;attribute name="__independent_subtree" rha:description="Treat this and all children as an independent subtree."/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__enforce_timeouts" rha:description="Consider a timeout for operations as fatal."/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__max_failures" rha:description="Maximum number of failures before returning a failure to a status check."/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__failure_expire_time" rha:description="Amount of time before a failure is forgotten."/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;ref name="CHILDREN"/&gt;
      &lt;/optional&gt;
    &lt;/element&gt;
  &lt;/define&gt;

</xsl:template>
</xsl:stylesheet>
