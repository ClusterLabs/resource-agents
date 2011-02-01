<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="text" indent="no"/>

<xsl:template match="parameter">
.TP
.B <xsl:value-of select="@name"/><xsl:text>
</xsl:text>
<xsl:value-of select="normalize-space(longdesc)"/><xsl:text>
</xsl:text>
<xsl:if test="@primary = 1">This is the defining attribute for the <b><xsl:value-of select="/resource-agent/@name"/></b> resource type, and will be shown in logs.
</xsl:if>
<xsl:if test="@unique = 1 or @primary = 1">No other instances of the 
.B <xsl:value-of select="/resource-agent/@name"/>
resource may have the same value for the 
.B <xsl:value-of select="@name"/>
parameter.
</xsl:if>
<xsl:if test="@required = 1 or @primary = 1">This parameter is required; the resource manager will ignore specification of a resource without this parameter.</xsl:if>
<xsl:if test="@reconfig = 1">You may safely change this attribute on the fly; doing so will not cause a restart of the resource or its children.
</xsl:if>
Content: <xsl:value-of select="content/@type"/><xsl:text>
</xsl:text>
<xsl:if test="content/@default">Default Value: <xsl:value-of select="content/@default"/>
</xsl:if>
</xsl:template>

<xsl:template match="action">
.TP
\fB<xsl:value-of select="@name"/>\fp<xsl:if test="@timeout"> (timeout: <xsl:value-of select="@timeout"/>) </xsl:if>
<xsl:choose>
<xsl:when test="@name = 'start'">
This starts the resource.
</xsl:when>
<xsl:when test="@name = 'stop'">
This stops the resource.
</xsl:when>
<xsl:when test="@name = 'monitor'">
<xsl:if test="@depth">
Depth <xsl:value-of select="@depth"/>.
</xsl:if>
This checks the status of the resource.  This is specified in the OCF Resource Agent API, but not LSB compliant.  This is synonymous with
.B status
on some resource managers.
</xsl:when>
<xsl:when test="@name = 'validate-all'">
Given (minimally) all required parameters to start or check the status of the resource, validate that those parameters are correct as much as possible.
</xsl:when>
<xsl:when test="@name = 'meta-data'">
Display the XML metadata describing this resource.
</xsl:when>
<xsl:when test="@name = 'reload'">Reconfigure the resource in-place with the new given parameters.
</xsl:when>
<xsl:when test="@name = 'recover'">
Attempt to recover the resource in-place without affecting dependencies.  If this fails, the resource manager will try more forceful recovery (such as stop-start).
</xsl:when>
<!-- known non-OCF stuff -->
<xsl:when test="@name = 'status'">
<xsl:if test="@depth">Depth <xsl:value-of select="@depth"/>.
</xsl:if>
This checks the status of the resource.  This is LSB compliant, but not specified by the OCF Resource Agent API.  This is synonymous with
.B monitor
on some resource managers.
</xsl:when>
<xsl:when test="@name = 'reconfig'">
Reconfigure the resource in-place with the new given parameters.
</xsl:when>
<xsl:when test="@name = 'verify-all'">
Given (minimally) all required parameters to start or check the status of the resource, validate that those parameters are correct as much as possible.  This is a misinterpretation of the 
.B validate-all
action, and should be fixed.  Please report a bug.
</xsl:when>
<xsl:when test="@name = 'promote'">
If this resource was the slave instance of the
resource, promote it to master status.
</xsl:when>
<xsl:when test="@name = 'demote'">
If this resource was the master instance of the
resource, demote it to slave status.
</xsl:when>
<xsl:when test="@name = 'migrate'">
Migrate this resource to another node in the cluster.
</xsl:when>
<!-- Ehhh -->
<xsl:otherwise>
The operational behavior of this is not known.
</xsl:otherwise>
</xsl:choose>
</xsl:template>

<xsl:template match="child">
.PP
<xsl:value-of select="@type"/> -
Started at level <xsl:value-of select="@start"/>.
Stopped at level <xsl:value-of select="@stop"/>.
</xsl:template>
<xsl:template match="/resource-agent">.TH RESOURCE_AGENT 8 2009-01-20 "<xsl:value-of select="@name"/> (Resource Agent)"
.SH
<xsl:value-of select="@name"/>
Cluster Resource Agent

.SH DESCRIPTION
<xsl:value-of select="normalize-space(longdesc)"/>

.SH PARAMETERS
<xsl:apply-templates select="parameters"/>

.SH RGMANAGER INTERNAL PARAMETERS
.TP
.B __enforce_timeouts
If set to 1, an operation exceeding the defined timeout will be considered
a failure of that operation.  Note that fail-to-stop is critical, and causes
a service to enter the failed state.

.TP
.B __independent_subtree
If set to 1, failure of a status operation of this resource or any of its
children will be considered non-fatal unless a restart of this resource and
all of its children also fails.

.SH ACTIONS
<xsl:apply-templates select="actions"/>

.SH RGMANAGER NOTES
<xsl:if test="special/@tag = 'rgmanager'">
<xsl:if test="special/attributes/@maxinstances">
.PP
An instatnce of this resource defined in the
.B &lt;resources&gt;
section of
.B cluster.conf
can be referenced in the resource
tree at most <xsl:value-of select="special/attributes/@maxinstances"/>
time(s).  All subsequent references to this resource will be ignored.
</xsl:if>
<xsl:if test="special/attributes/@root">
.PP
This is the root resource class.  Other resource 
types must be attached as children to this resource
class.
</xsl:if>
<xsl:if test="special/child/@type">
.PP
Known Child Types:
<xsl:apply-templates select="special"/>
</xsl:if>
</xsl:if>
.SH REFERENCES
.PP
http://www.opencf.org/cgi-bin/viewcvs.cgi/specs/ra/resource-agent-api.txt?rev=HEAD - The Open Cluster Framework Resource Agent Application Programming Interface draft version 1.0

.PP
http://www.linux-foundation.org/spec/refspecs/LSB_3.1.0/LSB-Core-generic/LSB-Core-generic/iniscrptact.html - Linux Standards Base v3.1.0 - Init Script Actions

.PP
http://sources.redhat.com/cluster/wiki/RGManager - Linux-cluster Resource Group Manager information
</xsl:template>

</xsl:stylesheet>
