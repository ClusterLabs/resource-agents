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
     <xsl:apply-templates select="action" mode="synopsis"/>
   </group>
 </xsl:template>

 <xsl:template match="action" mode="synopsis">
   <arg choice="plain" rep="norepeat">
     <xsl:value-of select="@name"/>
    </arg>
 </xsl:template>


 <!-- Mode Description --> 
 <xsl:template match="resource-agent" mode="description">
    <refsection>
      <title>Description</title>
      <xsl:apply-templates mode="description"/>
    </refsection>
    </xsl:template>

  <xsl:template match="longdesc" mode="description">
    <para>
     <xsl:apply-templates mode="description"/>
    </para>
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
    <xsl:apply-templates select="text()" mode="parameters"/>
  </xsl:template>
  
  <xsl:template match="shortdesc" mode="parameters">
    <xsl:apply-templates select="text()" mode="parameters"/>
  </xsl:template>
  
  <xsl:template match="content" mode="parameters">
    <xsl:if test="@type != '' or @default != ''">
      <xsl:text> (</xsl:text>
      <xsl:choose>
	<xsl:when test="@required = 1">
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
	  <xsl:otherwise>
	    <xsl:text>no default</xsl:text>
	  </xsl:otherwise>
      </xsl:choose>
      <xsl:text>)</xsl:text>
    </xsl:if>
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
	<xsl:text>primitive example_</xsl:text>
	<xsl:value-of select="@name"/>
	<xsl:text> </xsl:text>
	<xsl:value-of select="$class"/>
	<xsl:text>:</xsl:text>
	<xsl:value-of select="$provider"/>
	<xsl:text>:</xsl:text>
	<xsl:value-of select="@name"/>
	<xsl:text> \
</xsl:text>
	<xsl:text>  params \
</xsl:text>
	<xsl:apply-templates select="parameters" mode="example"/>
	<!-- Insert a suggested allow-migrate meta attribute if the
	     resource agent supports migration -->
	<xsl:if test="actions/action/@name = 'migrate_from' or actions/action/@name = 'migrate_to'">
	  <xsl:text>
  meta allow-migrate="true" \</xsl:text>
	</xsl:if>
	<xsl:apply-templates select="actions" mode="example"/>
      </programlisting>
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
    <xsl:apply-templates select="action[@name = 'monitor']" mode="example"/>
  </xsl:template>

  <xsl:template match="action" mode="example">
    <xsl:text>
  op </xsl:text>
    <xsl:value-of select="@name"/>
    <xsl:text> </xsl:text>
    <xsl:apply-templates select="@*" mode="example"/>
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
