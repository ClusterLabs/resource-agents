#!/bin/sh

cat <<EOF
<?xml version='1.0' encoding='utf-8' ?>
<!DOCTYPE appendix PUBLIC "-//OASIS//DTD DocBook XML V4.4//EN" "http://www.oasis-open.org/docbook/xml/4.4/docbookx.dtd">
<appendix id="ap-ra-man-pages">
  <title>Resource agent manual pages</title>
EOF

for manpage in `printf "%s\n" $@ | sort -f`; do
    cat <<EOF
  <xi:include href="./$manpage" xmlns:xi="http://www.w3.org/2001/XInclude"/>
EOF
done

cat <<EOF
</appendix>
EOF
