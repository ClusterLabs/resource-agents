#!/bin/sh

cat <<EOF
<?xml version='1.0' encoding='utf-8' ?>
<!DOCTYPE appendix PUBLIC "-//OASIS//DTD DocBook XML V4.5//EN" "http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd">
<appendix id="ap-ra-man-pages">
  <title>Resource agent manual pages</title>
EOF

for manpage in $@; do
    cat <<EOF
  <xi:include href="./$manpage" xmlns:xi="http://www.w3.org/2001/XInclude"/>
EOF
done

cat <<EOF
</appendix>
EOF
