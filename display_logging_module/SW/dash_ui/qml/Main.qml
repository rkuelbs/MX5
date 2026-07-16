import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Miata.Dash

ApplicationWindow {
    id: window

    // Defaults keep the form previewable in Qt Design Studio. C++ supplies
    // the production values as initial root properties before QML loads.
    property var signalModel: null
    property bool showFullScreen: false
    property string dataSourceName: qsTr("Design preview")
    property var inputController: null
    property var navigationController: null
    property var warningManager: null
    property var signalProvider: null
    property var presentationProvider: null
    property var historyStore: null
    property var replayController: null

    width: 1920
    height: 720
    minimumWidth: 900
    minimumHeight: 500
    visible: true
    visibility: window.showFullScreen ? Window.FullScreen : Window.Windowed
    title: qsTr("Miata Dash")
    color: "#090c10"

    FocusScope {
        anchors.fill: parent
        focus: true
        Component.onCompleted: forceActiveFocus()

        Keys.onPressed: function(event) {
            if (window.inputController)
                event.accepted = window.inputController.handleKey(event.key, event.isAutoRepeat)
        }

        StackLayout {
            anchors.fill: parent
            currentIndex: window.navigationController ? window.navigationController.pageIndex : 0

            DrivePage {
                sourceName: window.dataSourceName
                signalProvider: window.signalProvider
                presentationProvider: window.presentationProvider
            }

            DiagnosticsPage {
                dataModel: window.signalModel
                sourceName: window.dataSourceName
                historyStore: window.historyStore
                replayController: window.replayController
            }
        }

        Rectangle {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 16
            width: warningCountLabel.implicitWidth + 28
            height: 38
            radius: 19
            color: "#7c2d12"
            visible: window.warningManager
                     && window.warningManager.activeCount > 0
                     && !window.warningManager.overlayVisible

            Label {
                id: warningCountLabel
                anchors.centerIn: parent
                text: window.warningManager
                      ? qsTr("%1 ACTIVE").arg(window.warningManager.activeCount) : ""
                color: "#fed7aa"
                font.bold: true
                font.pixelSize: 13
            }
        }

        DashMenuOverlay {
            anchors.fill: parent
            z: 10
            navigationController: window.navigationController
        }

        WarningOverlay {
            anchors.fill: parent
            z: 20
            warningManager: window.warningManager
        }
    }
}
