pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property var navigationController

    visible: root.navigationController ? root.navigationController.menuOpen : false

    Rectangle {
        anchors.fill: parent
        color: "#99000000"

        MouseArea {
            anchors.fill: parent
            onClicked: root.navigationController.closeMenu()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 520
        height: 144 + menuList.count * 64
        radius: 12
        color: "#111923"
        border.color: "#334155"
        border.width: 2

        Label {
            id: title
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 72
            leftPadding: 24
            text: qsTr("Dash menu")
            color: "#f8fafc"
            font.pixelSize: 28
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }

        ListView {
            id: menuList
            anchors.top: title.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: instructions.top
            anchors.margins: 12
            spacing: 4
            interactive: false
            model: root.navigationController ? root.navigationController.pageTitles : []
            currentIndex: root.navigationController ? root.navigationController.menuIndex : 0

            delegate: Rectangle {
                id: menuRow
                required property int index
                required property string modelData

                width: menuList.width
                height: 60
                radius: 6
                color: menuList.currentIndex === menuRow.index ? "#0c4a6e" : "#17202b"
                border.color: menuList.currentIndex === menuRow.index ? "#38bdf8" : "transparent"

                Label {
                    anchors.fill: parent
                    leftPadding: 20
                    text: menuRow.modelData
                    color: "#f1f5f9"
                    font.pixelSize: 22
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.navigationController.activateMenuIndex(menuRow.index)
                }
            }
        }

        Label {
            id: instructions
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 48
            text: qsTr("UP/DOWN select    ACK enter    MENU close")
            color: "#94a3b8"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
