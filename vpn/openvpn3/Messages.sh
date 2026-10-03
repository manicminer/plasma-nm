#! /usr/bin/env bash
$XGETTEXT `find . -name "*.cpp" -not -path "./autotests/*"` -o $podir/plasmanetworkmanagement_openvpn3ui.pot
