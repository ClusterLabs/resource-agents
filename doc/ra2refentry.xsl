<?xml version="1.0" encoding="UTF-8"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
 version="1.0">

 <xsl:output indent="yes"
             doctype-public="-//OASIS//DTD DocBook XML V4.4//EN"
             doctype-system="http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd"/>

 <!--<xsl:strip-space elements="longdesc shortdesc"/>-->

 <!-- Package name. -->
 <xsl:param name="package">resource-agents</xsl:param>

 <!-- Package version number. Must be passed in. -->
 <xsl:param name="version"/>

 <!-- RA class -->
 <xsl:param name="class">ocf</xsl:param>

 <!-- RA provider -->
 <xsl:param name="provider">heartbeat</xsl:param>

 <!-- Man volume number -->
 <xsl:param name="manvolum">7</xsl:param>

 <!--  -->
 <xsl:param name="variable.prefix"/>

 <!-- Separator between different action/@name -->
 <xsl:param name="separator"> | </xsl:param>

 <xsl:variable name="manpagetitleprefix"><xsl:value-of select="$class"/>_<xsl:value-of select="$provider"/>_</xsl:variable>

 <xsl:template match="/">
  <refentry>
   <xsl:apply-templates mode="root"/>
  </refentry>
 </xsl:template>

 <xsl:template match="resource-agent" mode="root">
  <xsl:param name="this" select="self::resource-agent"/>
  <xsl:attribute name="id">
    <xsl:text>re-ra-</xsl:text>
    <xsl:value-of select="@name"/>
  </xsl:attribute>
  <xsl:apply-templates select="$this" mode="refentryinfo"/>
  <xsl:apply-templates select="$this" mode="refmeta"/>
  <xsl:apply-templates select="$this" mode="refnamediv"/>   
  <xsl:apply-templates select="$this" mode="synopsis"/>
  <xsl:apply-templates select="$this" mode="description"/>
  <xsl:apply-templates select="$this" mode="parameters"/>
  <xsl:apply-templates select="$this" mode="actions"/>
  <xsl:apply-templates select="$this" mode="example"/>
  <xsl:apply-templates select="$this" mode="seealso"/>
 </xsl:template>


 <!-- Empty Templates -->
 <xsl:template match="node()" mode="root"/>
 <xsl:template match="*" mode="refmeta"/>
 <xsl:template match="*" mode="refnamediv"/>

 <xsl:template match="*" mode="synopsis"/>
 <xsl:template match="*" mode="description"/>
 <xsl:template match="*" mode="parameters"/>

 <!-- Mode refentryinfo -->
 <xsl:template match="resource-agent" mode="refentryinfo">
   <refentryinfo>
     <productname><xsl:value-of select="$package"/></productname>
     <productnumber><xsl:value-of select="$version"/></productnumber>
     <corpauthor>Linux-HA contributors (see the resource agent source for information about individual authors)</corpauthor>
   </refentryinfo>
 </xsl:template>

 <!-- Mode refmeta -->
 <xsl:template match="resource-agent" mode="refmeta">
  <refmeta>
    <refentrytitle><xsl:value-of select="$manpagetitleprefix"/><xsl:value-of select="@name"/></refentrytitle>
    <manvolnum><xsl:value-of select="$manvolum"/></manvolnum>
    <refmiscinfo class="manual">OCF resource agents</refmiscinfo>
  </refmeta>
 </xsl:template>

 <!-- Mode refnamediv -->
 <xsl:template match="resource-agent" mode="refnamediv">
  <refnamediv>
    <refname><xsl:value-of select="$manpagetitleprefix"/><xsl:value-of select="@name"/></refname>
    <refpurpose><xsl:apply-templates select="shortdesc"/></refpurpose>
  </refnamediv>
 </xsl:template>


 <!-- Mode synopsis -->
 <xsl:template match="resource-agent" mode="synopsis">
   <refsynopsisdiv>
     <cmdsynopsis sepchar=" ">
       <command moreinfo="none">
	 <xsl:value-of select="@name"/>
       </command>
       <xsl:apply-templates select="actions" mode="synopsis"/>
     </cmdsynopsis>
   </refsynopsisdiv>
 </xsl:template>

 <xsl:template match="actions" mode="synopsis">
   <group choice="opt" rep="norepeat">
     <xsl:apply-templates select="action[@name = 'start'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'stop'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'status'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'monitor'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'migrate_to'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'migrate_from'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'promote'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'demote'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'meta-data'][1]" mode="synopsis"/>
     <xsl:apply-templates select="action[@name = 'validate-all'][1]" mode="synopsis"/>
   </group>
 </xsl:template>

 <xsl:template match="action" mode="synopsis">
   <arg choice="plain" rep="norepeat">
     <xsl:value-of select="@name"/>
    </arg>
 </xsl:template>


 <!-- Mode Description --> 

 <!-- break string into <para> elements on linefeeds -->

<xsl:template name="break_into_para">
    <xsl:param name="string" />

    <xsl:variable name="lf" select="'&#xA;&#xA;'" />
    <xsl:choose>
        <xsl:when test="contains($string, $lf)">
            <xsl:variable name="first" select="substring-before($string, $lf)" />

            <para>
                <xsl:value-of select="'&#xA;'" />
                <xsl:value-of select="$first"/>
                <xsl:value-of select="'&#xA;'" />
            </para>
            <xsl:value-of select="'&#xA;'" />

           <!-- recursively call an remaining string --> 
            <xsl:call-template name="break_into_para">
                <xsl:with-param name="string"
                    select="substring-after($string, $lf)" />
            </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
            <para>
                <xsl:value-of select="$string"/>
            </para>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template> 

  <xsl:template match="resource-agent" mode="description">
    <refsection>
      <title>Description</title>
      <xsl:apply-templates mode="description"/>
    </refsection>
    </xsl:template>

  <xsl:template match="text()" mode="longdesc">
      <xsl:call-template name="break_into_para">
          <xsl:with-param name="string" select="." />
      </xsl:call-template>
  </xsl:template>

  <xsl:template match="longdesc" mode="description">
     <xsl:apply-templates mode="longdesc"/>
  </xsl:template>

  <xsl:template match="actions" mode="description">
    <xsl:if test="action[@name = 'migrate_from' or @name = 'migrate_to']">
      <para>This resource agent may be configured for <emphasis>native
      migration</emphasis> if available in the cluster manager. For
      Pacemaker, the
      <parameter>allow-migrate=&quot;true&quot;</parameter> meta
      attribute enables native migration.</para>
    </xsl:if>
    <xsl:apply-templates mode="longdesc"/>
  </xsl:template>

  <!-- Mode Parameters -->
  <xsl:template match="resource-agent" mode="parameters">
    <refsection>
      <title>Supported Parameters</title>
      <xsl:choose>
	<xsl:when test="parameters">
	  <xsl:apply-templates mode="parameters"/>
	</xsl:when>
	<xsl:otherwise>
	  <para>
	    <xsl:text>This resource agent does not support any parameters.</xsl:text>
	  </para>
	</xsl:otherwise>
      </xsl:choose>
    </refsection>
  </xsl:template>

  <xsl:template match="resource-agent/shortdesc|resource-agent/longdesc" mode="parameters"/>

  <xsl:template match="parameters" mode="parameters">
    <variablelist>
      <xsl:apply-templates mode="parameters"/>
    </variablelist>
  </xsl:template>
  
  
  <xsl:template match="parameter" mode="parameters">
   <varlistentry>
    <term>
      <option><xsl:value-of select="concat($variable.prefix, @name)"/></option>
    </term>
    <listitem>
      <para>
	<xsl:apply-templates select="longdesc" mode="parameters"/>
	<xsl:apply-templates select="content" mode="parameters"/>
      </para>
    </listitem>
   </varlistentry>
  </xsl:template>
  
  <xsl:template match="longdesc" mode="parameters">
    <xsl:apply-templates select="node()" mode="longdesc"/>
  </xsl:template>
  
  <xsl:template match="shortdesc" mode="parameters">
    <xsl:apply-templates select="text()" mode="parameters"/>
  </xsl:template>
  
  <xsl:template match="content" mode="parameters">
    <xsl:if test="@type != '' or @default != ''">
      <xsl:text> (</xsl:text>
      <xsl:if test="../@unique = 1">
	<xsl:text>unique, </xsl:text>
      </xsl:if>
      <xsl:choose>
	<xsl:when test="../@required = 1">
	  <xsl:text>required</xsl:text>
	</xsl:when>
	<xsl:otherwise>
	  <xsl:text>optional</xsl:text>
	</xsl:otherwise>
      </xsl:choose>
      <xsl:text>, </xsl:text>
      <xsl:if test="@parameter != ''">
	<xsl:value-of select="@type"/>
	<xsl:text>, </xsl:text>
      </xsl:if>
      <xsl:if test="@type != ''">
	<xsl:value-of select="@type"/>
	<xsl:text>, </xsl:text>
      </xsl:if>
      <xsl:choose>
	  <xsl:when test="@default != ''">
	    <xsl:text>default </xsl:text>
	    <code>
	      <xsl:value-of select="@default"/>
	    </code>
	  </xsl:when>
	  <xsl:when test="@type='boolean' and @default = ''">
	    <xsl:text>default </xsl:text>
	    <code>false</code>
	  </xsl:when>
	  <xsl:otherwise>
	    <xsl:text>no default</xsl:text>
	  </xsl:otherwise>
      </xsl:choose>
      <xsl:text>)</xsl:text>
    </xsl:if>
  </xsl:template>


  <!-- Mode Actions -->
  <xsl:template match="resource-agent" mode="actions">
    <refsection>
      <title>Supported Actions</title>
      <xsl:choose>
	<xsl:when test="actions">
	  <xsl:apply-templates select="actions" mode="actions"/>
	</xsl:when>
	<xsl:otherwise>
	  <!-- This should actually never happen. Every RA must
	       advertise the actions it supports. -->
	  <para>
	    <xsl:text>This resource agent does not advertise any supported actions.</xsl:text>
	  </para>
	</xsl:otherwise>
      </xsl:choose>
    </refsection>
  </xsl:template>

  <xsl:template match="actions" mode="actions">
    <para>This resource agent supports the following actions (operations):</para>
    <variablelist>
      <xsl:apply-templates select="action" mode="actions"/>
    </variablelist>
  </xsl:template>

  <xsl:template match="action" mode="actions">
   <varlistentry>
    <term>
      <option>
	<xsl:value-of select="@name"/>
	<xsl:if test="@role != ''">
	  <xsl:text> (</xsl:text>
	  <xsl:value-of select="@role"/>
	  <xsl:text> role)</xsl:text>
	</xsl:if>
      </option>
    </term>
    <listitem>
      <para>
	<xsl:choose>
	  <xsl:when test="@name = 'start'">
	    <xsl:text>Starts the resource.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'stop'">
	    <xsl:text>Stops the resource.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'status'">
	    <xsl:text>Performs a status check.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'monitor'">
	    <xsl:text>Performs a detailed status check.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'promote'">
	    <xsl:text>Promotes the resource to the Master role.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'demote'">
	    <xsl:text>Demotes the resource to the Slave role.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'migrate_from'">
	    <xsl:text>Executes steps necessary for migrating the
	    resource </xsl:text>
	    <emphasis>away from</emphasis>
	    <xsl:text> the node.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'migrate_to'">
	    <xsl:text>Executes steps necessary for migrating the
	    resource </xsl:text>
	    <emphasis>to</emphasis>
	    <xsl:text> the node.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'validate-all'">
	    <xsl:text>Performs a validation of the resource configuration.</xsl:text>
	  </xsl:when>
	  <xsl:when test="@name = 'meta-data'">
	    <xsl:text>Retrieves resource agent metadata (internal use only).</xsl:text>
	  </xsl:when>
	</xsl:choose>
	<xsl:if test="@timeout != ''">
	  <xsl:text> Suggested minimum timeout: </xsl:text>
	  <xsl:value-of select="@timeout"/>
	  <xsl:text>.</xsl:text>
	</xsl:if>
	<xsl:if test="@interval != ''">
	  <xsl:text> Suggested interval: </xsl:text>
	  <xsl:value-of select="@interval"/>
	  <xsl:text>.</xsl:text>
	</xsl:if>
      </para>
    </listitem>
   </varlistentry>
  </xsl:template>


  <!-- Mode Example -->
  <xsl:template match="resource-agent" mode="example">
    <refsection>
      <title>Example</title>
      <para>
	<xsl:text>The following is an example configuration for a </xsl:text>
	<xsl:value-of select="@name"/>
	<xsl:text> resource using the </xsl:text>
	<citerefentry><refentrytitle>crm</refentrytitle><manvolnum>8</manvolnum></citerefentry>
	<xsl:text> shell:</xsl:text>
      </para>
      <programlisting>
	<xsl:text>primitive p_</xsl:text>
	<xsl:value-of select="@name"/>
	<xsl:text> </xsl:text>
	<xsl:value-of select="$class"/>
	<xsl:text>:</xsl:text>
	<xsl:value-of select="$provider"/>
	<xsl:text>:</xsl:text>
	<xsl:choose>
	  <xsl:when test="parameters/parameter[@required = 1]">
	    <xsl:value-of select="@name"/>
	    <xsl:text> \
  params \
</xsl:text>
	    <xsl:apply-templates select="parameters" mode="example"/>
	  </xsl:when>
	  <xsl:otherwise>
	  <xsl:value-of select="@name"/><xsl:text> \</xsl:text>
	  </xsl:otherwise>
	</xsl:choose>
	<!-- Insert a suggested allow-migrate meta attribute if the
	     resource agent supports migration -->
	<xsl:if test="actions/action[@name = 'migrate_from' or @name = 'migrate_to']">
	  <xsl:text>
  meta allow-migrate="true" \</xsl:text>
	</xsl:if>
	<xsl:apply-templates select="actions" mode="example"/>
      </programlisting>
      <!-- Insert a master/slave set definition if the resource
      agent supports promotion and demotion -->
      <xsl:if test="actions/action/@name = 'promote' and actions/action/@name = 'demote'">
	<programlisting>
	  <xsl:text>ms ms_</xsl:text>
	  <xsl:value-of select="@name"/>
	  <xsl:text> p_</xsl:text>
	  <xsl:value-of select="@name"/>
	<xsl:text> \
  meta notify="true" interleave="true"</xsl:text>
	</programlisting>
      </xsl:if>
    </refsection>
  </xsl:template>

  <xsl:template match="parameters" mode="example">
    <xsl:apply-templates select="parameter[@required = 1]" mode="example"/>
  </xsl:template>
  
  <xsl:template match="parameter" mode="example">
    <xsl:text>    </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text>=</xsl:text>
    <xsl:apply-templates select="content" mode="example"/>
    <xsl:text> \</xsl:text>
    <xsl:if test="following-sibling::parameter/@required = 1">
      <xsl:text>
</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="content" mode="example">
    <xsl:choose>
      <xsl:when test="@default != ''">
	<xsl:text>"</xsl:text>
	<xsl:value-of select="@default"/>
	<xsl:text>"</xsl:text>
      </xsl:when>
      <xsl:otherwise>
	<replaceable><xsl:value-of select="@type"/></replaceable>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="actions" mode="example">
    <!-- In the CRM shell example, show only the monitor action -->
    <xsl:apply-templates select="action[@name = 'monitor']" mode="example"/>
  </xsl:template>

  <xsl:template match="action" mode="example">
    <xsl:text>
  op </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text> </xsl:text>
    <xsl:apply-templates select="@*" mode="example"/>
    <xsl:if test="following-sibling::action/@name = 'monitor'">
      <xsl:text>\</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="action/@*" mode="example">
    <xsl:choose>
      <xsl:when test="name() = 'name'"><!-- suppress --></xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="name()"/>
	<xsl:text>="</xsl:text>
	<xsl:value-of select="current()"/>
	<xsl:text>" </xsl:text>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:if test="following-sibling::*">
      <xsl:text> </xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="longdesc" mode="example"/>

  <xsl:template match="shortdesc" mode="example"/>

  <xsl:template match="resource-agent" mode="seealso">
    <refsection>
      <title>See also</title>
      <para>
	<ulink>
	  <xsl:attribute name="url">
	    <xsl:text>http://www.linux-ha.org/wiki/</xsl:text>
	    <xsl:value-of select="@name"/>
	    <xsl:text>_(resource_agent)</xsl:text>
	  </xsl:attribute>
	</ulink>
      </para>
    </refsection>
  </xsl:template>

</xsl:stylesheet>
