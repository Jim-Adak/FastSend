# Third-party components

FastSend uses Qt 6.8.3 (Qt Core, GUI, Widgets, Network and their platform plugins),
copyright The Qt Company Ltd. and other contributors, dynamically linked under
the GNU Lesser General Public License version 3 (LGPL-3.0).

- Qt source: https://download.qt.io/archive/qt/6.8/6.8.3/single/
- Qt licenses and source notices: https://code.qt.io/cgit/qt/qtbase.git/tree/LICENSES?h=v6.8.3
- Full LGPL-3.0 and GPL-3.0 license texts are included in the licenses directory.
- Qt third-party notices: https://doc.qt.io/qt-6.8/licenses-used-in-qt.html

Qt libraries remain separate DLL files. Users may replace the dynamically linked
Qt libraries with compatible modified builds and may reverse engineer the
application to debug modifications to those libraries, as permitted by LGPL-3.0.
The MIT license for FastSend applies to this project's code, not to Qt.

Microsoft Visual C++ runtime files, if included by the Windows deployment tool,
are distributed under Microsoft's applicable runtime redistribution terms.
