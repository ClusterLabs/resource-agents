<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" indent="yes"/>
<xsl:template name="capitalize">
	<xsl:param name="value"/>
	<xsl:variable name="normalized" select="translate($value, '_abcdefghijklmnopqrstuvwrxyz', '-ABCDEFGHIJKLMNOPQRSTUVWRXYZ')"/>
	<xsl:value-of select="$normalized"/>
</xsl:template>

<xsl:template match="/resource-agent">
  &lt;define name="<xsl:call-template name="capitalize"><xsl:with-param name="value" select="@name"/></xsl:call-template>"&gt;
    &lt;element name="<xsl:value-of select="@name"/>"&gt;
      &lt;!-- <xsl:value-of select="normalize-space(shortdesc)"/> --&gt;
      &lt;choice&gt;
      &lt;group&gt;
        &lt;!-- rgmanager specific stuff --&gt;
        &lt;attribute name="ref"/&gt;
      &lt;/group&gt;
      &lt;group&gt;<xsl:for-each select="parameters/parameter">
		<xsl:choose>
			<xsl:when test="@required = 1 or @primary = 1">
        &lt;attribute name="<xsl:value-of select="@name"/>"/&gt;</xsl:when>
			<xsl:otherwise>
        &lt;optional&gt;
          &lt;attribute name="<xsl:value-of select="@name"/>"/&gt;
        &lt;/optional&gt;</xsl:otherwise>
		</xsl:choose>
	</xsl:for-each>
      &lt;/group&gt;
      &lt;/choice&gt;
      &lt;optional&gt;
        &lt;attribute name="__independent_subtree"/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__enforce_timeouts"/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__max_failures"/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;attribute name="__failure_expire_time"/&gt;
      &lt;/optional&gt;
      &lt;optional&gt;
        &lt;ref name="CHILDREN"/&gt;
      &lt;/optional&gt;
    &lt;/element&gt;
  &lt;/define&gt;

</xsl:template>
</xsl:stylesheet>
