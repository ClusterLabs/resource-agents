<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:int="__internal__"
    exclude-result-prefixes="int">
    <xsl:output method="text" indent="no"/>

<xsl:param name="init-indent" select="'  '"/>
<xsl:param name="indent" select="'  '"/>


<!--
  helpers
  -->

<int:common-optional-parameters>
    <int:parameter name="__independent_subtree">
        <int:shortdesc>
            Treat this and all children as an independent subtree.
        </int:shortdesc>
    </int:parameter>
    <int:parameter name="__enforce_timeouts">
        <int:shortdesc>
            Consider a timeout for operations as fatal.
        </int:shortdesc>
    </int:parameter>
    <int:parameter name="__max_failures">
        <int:shortdesc>
            Maximum number of failures before returning a failure to
            a status check.
        </int:shortdesc>
    </int:parameter>
    <int:parameter name="__failure_expire_time">
        <int:shortdesc>
            Amount of time before a failure is forgotten.
        </int:shortdesc>
    </int:parameter>
    <int:parameter name="__max_restarts">
        <int:shortdesc>
            Maximum number restarts for an independent subtree before
            giving up.
        </int:shortdesc>
    </int:parameter>
    <int:parameter name="__restart_expire_time">
        <int:shortdesc>
            Amount of time before a failure is forgotten for
            an independent subtree.
        </int:shortdesc>
    </int:parameter>
</int:common-optional-parameters>

<xsl:variable name="SP" select="' '"/>
<xsl:variable name="NL" select="'&#xA;'"/>
<xsl:variable name="NLNL" select="'&#xA;&#xA;'"/>
<xsl:variable name="Q" select="'&quot;'"/>
<xsl:variable name="TS" select="'&lt;'"/>
<xsl:variable name="TSc" select="'&lt;/'"/>
<xsl:variable name="TE" select="'&gt;'"/>
<xsl:variable name="TEc" select="'/&gt;'"/>

<xsl:template name="comment">
    <xsl:param name="text" select="''"/>
    <xsl:param name="indent" select="''"/>
    <xsl:if test="$indent != 'none'">
        <xsl:value-of select="concat($init-indent, $indent)"/>
    </xsl:if>
    <xsl:value-of select="concat($TS, '!-- ', $text, ' --',$TE)"/>
</xsl:template>

<xsl:template name="tag-start">
    <xsl:param name="name"/>
    <xsl:param name="attrs" select="''"/>
    <xsl:param name="indent" select="''"/>
    <xsl:if test="$indent != 'none'">
        <xsl:value-of select="concat($init-indent, $indent)"/>
    </xsl:if>
    <xsl:value-of select="concat($TS, $name)"/>
    <xsl:if test="$attrs != ''">
        <xsl:value-of select="concat($SP, $attrs)"/>
    </xsl:if>
    <xsl:value-of select="$TE"/>
</xsl:template>

<xsl:template name="tag-end">
    <xsl:param name="name"/>
    <xsl:param name="attrs" select="''"/>
    <xsl:param name="indent" select="''"/>
    <xsl:if test="$indent != 'none'">
        <xsl:value-of select="concat($init-indent, $indent)"/>
    </xsl:if>
    <xsl:value-of select="concat($TSc, $name)"/>
    <xsl:if test="$attrs != ''">
        <xsl:value-of select="concat($SP, $attrs)"/>
    </xsl:if>
    <xsl:value-of select="$TE"/>
</xsl:template>

<xsl:template name="tag-self">
    <xsl:param name="name"/>
    <xsl:param name="attrs" select="''"/>
    <xsl:param name="indent" select="''"/>
    <xsl:if test="$indent != 'none'">
        <xsl:value-of select="concat($init-indent, $indent)"/>
    </xsl:if>
    <xsl:value-of select="concat($TS, $name)"/>
    <xsl:if test="$attrs != ''">
        <xsl:value-of select="concat($SP, $attrs)"/>
    </xsl:if>
    <xsl:value-of select="$TEc"/>
</xsl:template>

<xsl:template name="capitalize">
    <xsl:param name="value"/>
    <xsl:value-of select="translate($value,
                                    '_abcdefghijklmnopqrstuvwrxyz',
                                    '-ABCDEFGHIJKLMNOPQRSTUVWRXYZ')"/>
</xsl:template>


<!--
  proceed
  -->

<xsl:template match="/resource-agent">
    <xsl:value-of select="$NL"/>

    <!-- define name=... (start) -->
    <xsl:variable name="capitalized">
        <xsl:call-template name="capitalize">
            <xsl:with-param name="value" select="@name"/>
        </xsl:call-template>
    </xsl:variable>
    <xsl:call-template name="tag-start">
        <xsl:with-param name="name" select="'define'"/>
        <xsl:with-param name="attrs" select="concat(
            'name=', $Q, $capitalized, $Q)"/>
    </xsl:call-template>
    <xsl:value-of select="$NL"/>

        <!-- element name=... rha:description=... (start) -->
        <xsl:call-template name="tag-start">
            <xsl:with-param name="name" select="'element'"/>
            <xsl:with-param name="attrs" select="concat(
                'name=',            $Q, @name,                      $Q, $SP,
                'rha:description=', $Q, normalize-space(shortdesc), $Q)"/>
            <xsl:with-param name="indent" select="$indent"/>
        </xsl:call-template>
        <xsl:value-of select="$NL"/>

            <!-- choice (start) -->
            <xsl:call-template name="tag-start">
                <xsl:with-param name="name" select="'choice'"/>
                <xsl:with-param name="indent" select="concat($indent, $indent)"/>
            </xsl:call-template>
            <xsl:value-of select="$NL"/>

                <!-- group (start) -->
                <xsl:call-template name="tag-start">
                    <xsl:with-param name="name" select="'group'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

                    <!-- (comment) -->
                    <xsl:call-template name="comment">
                        <xsl:with-param name="text">
                            <xsl:text>rgmanager specific stuff</xsl:text>
                        </xsl:with-param>
                        <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                     $indent, $indent)"/>
                    </xsl:call-template>
                    <xsl:value-of select="$NL"/>

                    <!-- attribute name="ref" -->
                    <xsl:call-template name="tag-self">
                        <xsl:with-param name="name" select="'attribute'"/>
                        <xsl:with-param name="attrs" select="concat(
                            'name=',            $Q, 'ref',                    $Q, $SP,
                            'rha:description=', $Q, 'Reference to existing ',
                                                    @name, ' resource in ',
                                                    'the resources section.', $Q)"/>
                        <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                     $indent, $indent)"/>
                    </xsl:call-template>
                    <xsl:value-of select="$NL"/>

                <!-- group (end) -->
                <xsl:call-template name="tag-end">
                    <xsl:with-param name="name" select="'group'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

                <!-- group (start) -->
                <xsl:call-template name="tag-start">
                    <xsl:with-param name="name" select="'group'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

                <xsl:for-each select="parameters/parameter">
                    <xsl:choose>
                        <xsl:when test="@required = '1' or @primary = '1'">
                            <!-- attribute name=... rha:description=... -->
                            <xsl:call-template name="tag-self">
                                <xsl:with-param name="name" select="'attribute'"/>
                                <xsl:with-param name="attrs" select="concat(
                                    'name=',            $Q, @name,                      $Q, $SP,
                                    'rha:description=', $Q, normalize-space(shortdesc), $Q)"/>
                                <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                             $indent, $indent)"/>
                            </xsl:call-template>
                            <xsl:value-of select="$NL"/>
                        </xsl:when>
                        <xsl:otherwise>
                            <!-- optional (start) -->
                            <xsl:call-template name="tag-start">
                                <xsl:with-param name="name" select="'optional'"/>
                                <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                             $indent, $indent)"/>
                            </xsl:call-template>
                            <xsl:value-of select="$NL"/>

                                <!-- attribute name=... rha:description=... -->
                                <xsl:call-template name="tag-self">
                                    <xsl:with-param name="name" select="'attribute'"/>
                                    <xsl:with-param name="attrs" select="concat(
                                        'name=',            $Q, @name,                      $Q, $SP,
                                        'rha:description=', $Q, normalize-space(shortdesc), $Q)"/>
                                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                                 $indent, $indent,
                                                                                 $indent)"/>
                                </xsl:call-template>
                                <xsl:value-of select="$NL"/>

                            <!-- optional (end) -->
                            <xsl:call-template name="tag-end">
                                <xsl:with-param name="name" select="'optional'"/>
                                <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                             $indent, $indent)"/>
                            </xsl:call-template>
                            <xsl:value-of select="$NL"/>
                        </xsl:otherwise>
                    </xsl:choose>
                </xsl:for-each>

                <!-- group (end) -->
                <xsl:call-template name="tag-end">
                    <xsl:with-param name="name" select="'group'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

            <!-- choice (end) -->
            <xsl:call-template name="tag-end">
                <xsl:with-param name="name" select="'choice'"/>
                <xsl:with-param name="indent" select="concat($indent, $indent)"/>
            </xsl:call-template>
            <xsl:value-of select="$NL"/>

            <xsl:for-each select="document('')/*/int:common-optional-parameters/int:parameter">
                <!-- optional (start) -->
                <xsl:call-template name="tag-start">
                    <xsl:with-param name="name" select="'optional'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

                    <!-- attribute name=... rha:description=... -->
                    <xsl:call-template name="tag-self">
                        <xsl:with-param name="name" select="'attribute'"/>
                        <xsl:with-param name="attrs" select="concat(
                            'name=',            $Q, @name,                          $Q, $SP,
                            'rha:description=', $Q, normalize-space(int:shortdesc), $Q)"/>
                        <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                     $indent)"/>
                    </xsl:call-template>
                    <xsl:value-of select="$NL"/>

                <!-- optional (end) -->
                <xsl:call-template name="tag-end">
                    <xsl:with-param name="name" select="'optional'"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>
            </xsl:for-each>

            <!-- interleave (start) -->
            <xsl:call-template name="tag-start">
                <xsl:with-param name="name" select="'interleave'"/>
                <xsl:with-param name="indent" select="concat($indent, $indent)"/>
            </xsl:call-template>
            <xsl:value-of select="$NL"/>

                <!-- ref name="RESOURCEACTION" -->
                <xsl:call-template name="tag-self">
                    <xsl:with-param name="name" select="'ref'"/>
                    <xsl:with-param name="attrs" select="concat(
                        'name=', $Q, 'RESOURCEACTION', $Q)"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

                <!-- ref name="CHILDREN" -->
                <xsl:call-template name="tag-self">
                    <xsl:with-param name="name" select="'ref'"/>
                    <xsl:with-param name="attrs" select="concat(
                        'name=', $Q, 'CHILDREN', $Q)"/>
                    <xsl:with-param name="indent" select="concat($indent, $indent,
                                                                 $indent)"/>
                </xsl:call-template>
                <xsl:value-of select="$NL"/>

            <!-- interleave (end) -->
            <xsl:call-template name="tag-end">
                <xsl:with-param name="name" select="'interleave'"/>
                <xsl:with-param name="indent" select="concat($indent, $indent)"/>
            </xsl:call-template>
            <xsl:value-of select="$NL"/>

        <!-- element (end) -->
        <xsl:call-template name="tag-end">
            <xsl:with-param name="name" select="'element'"/>
            <xsl:with-param name="indent" select="$indent"/>
        </xsl:call-template>
        <xsl:value-of select="$NL"/>

    <!-- define (end) -->
    <xsl:call-template name="tag-end">
        <xsl:with-param name="name" select="'define'"/>
    </xsl:call-template>
    <xsl:value-of select="$NLNL"/>

</xsl:template>

</xsl:stylesheet>
