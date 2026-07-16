pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window

    required property var simulatorBackend
    required property string busDescription
    required property int statusRateHz

    readonly property var buttonDefinitions: [
        { "id": 0, "name": qsTr("DOWN"), "consumer": qsTr("Dash") },
        { "id": 1, "name": qsTr("UP"), "consumer": qsTr("Dash") },
        { "id": 2, "name": qsTr("MENU"), "consumer": qsTr("Dash") },
        { "id": 3, "name": qsTr("ACK / ENTER"), "consumer": qsTr("Dash") },
        { "id": 4, "name": qsTr("LEFT TURN"), "consumer": qsTr("PDM") },
        { "id": 5, "name": qsTr("RIGHT TURN"), "consumer": qsTr("PDM") },
        { "id": 6, "name": qsTr("WIPER"), "consumer": qsTr("PDM") },
        { "id": 7, "name": qsTr("FLASH"), "consumer": qsTr("PDM") }
    ]

    width: 1040
    height: 720
    minimumWidth: 850
    minimumHeight: 620
    visible: true
    title: qsTr("Miata WCM Simulator")
    color: "#0b1118"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("Wheel Control Module Simulator")
                    color: "#f8fafc"
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("CAN: %1    Status: %2 Hz").arg(window.busDescription).arg(window.statusRateHz)
                    color: "#7dd3fc"
                    font.pixelSize: 14
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Release all")
                onClicked: window.simulatorBackend.releaseAllButtons()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 4
            rowSpacing: 12
            columnSpacing: 12

            Repeater {
                model: window.buttonDefinitions

                delegate: Rectangle {
                    id: buttonCard
                    required property var modelData
                    readonly property bool active:
                        (window.simulatorBackend.inputs & (1 << buttonCard.modelData.id)) !== 0

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 150
                    radius: 10
                    color: buttonCard.active ? "#164e63" : "#131c26"
                    border.width: 3
                    border.color: buttonCard.active
                                  ? "#22d3ee"
                                  : buttonCard.modelData.id < 4 ? "#2563eb" : "#475569"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: padMouse.pressed ? "#0e7490" : buttonCard.active ? "#155e75" : "#1e293b"

                            Column {
                                anchors.centerIn: parent
                                spacing: 4
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: buttonCard.modelData.name
                                    color: "#f8fafc"
                                    font.pixelSize: 20
                                    font.bold: true
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("Button %1 · %2").arg(buttonCard.modelData.id)
                                          .arg(buttonCard.modelData.consumer)
                                    color: "#94a3b8"
                                    font.pixelSize: 12
                                }
                            }

                            MouseArea {
                                id: padMouse
                                anchors.fill: parent
                                onPressed: window.simulatorBackend.setButtonPressed(buttonCard.modelData.id, true)
                                onReleased: {
                                    if (!holdCheck.checked)
                                        window.simulatorBackend.setButtonPressed(buttonCard.modelData.id, false)
                                }
                                onCanceled: {
                                    if (!holdCheck.checked)
                                        window.simulatorBackend.setButtonPressed(buttonCard.modelData.id, false)
                                }
                            }
                        }

                        CheckBox {
                            id: holdCheck
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Hold / stuck")
                            checked: false
                            onToggled: window.simulatorBackend.setButtonPressed(
                                           buttonCard.modelData.id, checked)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: 10
            color: "#111923"
            border.color: "#334155"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 20

                ColumnLayout {
                    Layout.preferredWidth: 250
                    Label {
                        text: qsTr("Transmission faults")
                        color: "#f1f5f9"
                        font.pixelSize: 17
                        font.bold: true
                    }
                    CheckBox {
                        text: qsTr("Pause all transmission")
                        checked: window.simulatorBackend.paused
                        onToggled: window.simulatorBackend.paused = checked
                    }
                    CheckBox {
                        text: qsTr("Drop status frames")
                        checked: window.simulatorBackend.dropStatus
                        onToggled: window.simulatorBackend.dropStatus = checked
                    }
                    CheckBox {
                        text: qsTr("Drop event frames")
                        checked: window.simulatorBackend.dropEvents
                        onToggled: window.simulatorBackend.dropEvents = checked
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 250
                    CheckBox {
                        text: qsTr("Freeze status counter")
                        checked: window.simulatorBackend.freezeCounter
                        onToggled: window.simulatorBackend.freezeCounter = checked
                    }
                    RowLayout {
                        Label { text: qsTr("Counter step"); color: "#cbd5e1" }
                        SpinBox {
                            from: 1
                            to: 255
                            value: window.simulatorBackend.counterStep
                            onValueModified: window.simulatorBackend.counterStep = value
                        }
                    }
                    RowLayout {
                        Label { text: qsTr("Boost encoder"); color: "#cbd5e1" }
                        SpinBox {
                            from: 0
                            to: 255
                            value: window.simulatorBackend.boostEncoder
                            onValueModified: window.simulatorBackend.boostEncoder = value
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: "#0d141c"

                    Column {
                        anchors.centerIn: parent
                        spacing: 7
                        Label {
                            text: qsTr("Inputs  0x%1    Counter  %2")
                                  .arg(window.simulatorBackend.inputs.toString(16).padStart(2, "0"))
                                  .arg(window.simulatorBackend.counter)
                            color: "#f8fafc"
                            font.family: "monospace"
                            font.pixelSize: 18
                        }
                        Label {
                            text: qsTr("Status frames: %1    Event frames: %2")
                                  .arg(window.simulatorBackend.statusFramesGenerated)
                                  .arg(window.simulatorBackend.eventFramesGenerated)
                            color: "#94a3b8"
                            font.pixelSize: 14
                        }
                        Label {
                            text: qsTr("Last event: %1").arg(window.simulatorBackend.lastEvent)
                            color: "#fbbf24"
                            font.pixelSize: 14
                        }
                    }
                }
            }
        }
    }
}
