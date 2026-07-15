import QtQuick
import QtQuick.Controls

import Miata.Dash

ApplicationWindow {
    id: window

    // Defaults keep the form previewable in Qt Design Studio. C++ supplies
    // the production values as initial root properties before QML loads.
    property var signalModel: null
    property bool showFullScreen: false
    property string dataSourceName: qsTr("Design preview")

    width: 1920
    height: 720
    minimumWidth: 900
    minimumHeight: 500
    visible: true
    visibility: window.showFullScreen ? Window.FullScreen : Window.Windowed
    title: qsTr("Miata Dash Diagnostics")
    color: "#090c10"

    DiagnosticsPage {
        anchors.fill: parent
        dataModel: window.signalModel
        sourceName: window.dataSourceName
    }
}
