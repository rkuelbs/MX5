import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var warningManager

    visible: root.warningManager ? root.warningManager.overlayVisible : false

    Rectangle {
        anchors.fill: parent
        color: "#cc000000"
    }

    MouseArea {
        anchors.fill: parent
        // Unacknowledged warnings are modal for both wheel and pointer input.
    }

    Rectangle {
        anchors.centerIn: parent
        width: 1080
        height: 390
        radius: 18
        color: "#1b1112"
        border.width: 5
        border.color: root.warningManager
                      && root.warningManager.currentSeverity === "critical"
                      ? "#ef4444" : "#f59e0b"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 36
            spacing: 20

            Label {
                Layout.fillWidth: true
                text: root.warningManager ? root.warningManager.currentTitle : ""
                color: "#fff7ed"
                font.pixelSize: 48
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: root.warningManager ? root.warningManager.currentMessage : ""
                color: "#e2e8f0"
                font.pixelSize: 28
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Press ACK to acknowledge")
                color: "#fbbf24"
                font.pixelSize: 21
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
