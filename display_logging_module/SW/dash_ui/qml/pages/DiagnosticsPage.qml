pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var dataModel
    required property var historyStore
    property string sourceName: "No data source"
    property bool chartVisible: false
    property var replayController: null

    readonly property int selectWidth: 60
    readonly property int valueWidth: 150
    readonly property int unitWidth: 100
    readonly property int sourceWidth: 90
    readonly property int ageWidth: 100
    readonly property int statusWidth: 90

    Rectangle {
        anchors.fill: parent
        color: "#090c10"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                spacing: 1

                Label {
                    text: qsTr("Signal Diagnostics")
                    color: "#f1f5f9"
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }

                Label {
                    text: root.sourceName
                    color: "#7dd3fc"
                    font.pixelSize: 13
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: root.chartVisible ? qsTr("Signal list") : qsTr("Live plot")
                onClicked: root.chartVisible = !root.chartVisible
            }

            Label {
                text: qsTr("%1 signals").arg(root.dataModel ? root.dataModel.count : 0)
                color: "#94a3b8"
                font.pixelSize: 14
            }

            TextField {
                id: filterField
                Layout.preferredWidth: 300
                placeholderText: qsTr("Filter canonical signal name…")
                selectByMouse: true
                onTextChanged: if (root.dataModel) root.dataModel.filterText = text

                Keys.onEscapePressed: {
                    clear()
                    focus = false
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: 4
            color: "#17202b"
            visible: root.replayController && root.replayController.available

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 10

                Label {
                    text: qsTr("Replay %1 - %2")
                        .arg(root.replayController ? root.replayController.source.toUpperCase() : "")
                        .arg(root.replayController ? root.replayController.state : "")
                    color: "#7dd3fc"
                    font.bold: true
                }
                Button {
                    text: root.replayController && root.replayController.state === "playing"
                          ? qsTr("Pause") : qsTr("Play")
                    onClicked: {
                        if (root.replayController.state === "playing") root.replayController.pause()
                        else root.replayController.play()
                    }
                }
                Slider {
                    id: replayPosition
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, root.replayController ? root.replayController.durationMs : 1)
                    onMoved: if (root.replayController) root.replayController.seekMs(value)

                    Binding on value {
                        when: !replayPosition.pressed
                        value: root.replayController ? root.replayController.positionMs : 0
                    }
                }
                Label {
                    text: qsTr("%1 / %2 s")
                        .arg(root.replayController ? (root.replayController.positionMs / 1000).toFixed(1) : "0.0")
                        .arg(root.replayController ? (root.replayController.durationMs / 1000).toFixed(1) : "0.0")
                    color: "#cbd5e1"
                    font.family: "monospace"
                }
                ComboBox {
                    readonly property var speeds: [0.25, 0.5, 1, 2, 5, 10]
                    model: ["0.25x", "0.5x", "1x", "2x", "5x", "10x"]
                    currentIndex: Math.max(0, speeds.indexOf(
                        root.replayController ? root.replayController.speedFactor : 1))
                    onActivated: if (root.replayController)
                        root.replayController.setPlaybackSpeed(speeds[currentIndex])
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: "#17202b"
            radius: 4

            Row {
                anchors.fill: parent

                Label {
                    width: root.selectWidth
                    height: parent.height
                    text: qsTr("Plot")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    font.bold: true
                }
                Label {
                    width: parent.width - root.selectWidth - root.valueWidth - root.unitWidth
                           - root.sourceWidth - root.ageWidth - root.statusWidth
                    height: parent.height
                    leftPadding: 10
                    text: qsTr("Canonical signal")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    font.bold: true
                }
                Label {
                    width: root.valueWidth
                    height: parent.height
                    text: qsTr("Value")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 12
                    font.bold: true
                }
                Label {
                    width: root.unitWidth
                    height: parent.height
                    text: qsTr("Unit")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    font.bold: true
                }
                Label {
                    width: root.sourceWidth
                    height: parent.height
                    text: qsTr("Source")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    font.bold: true
                }
                Label {
                    width: root.ageWidth
                    height: parent.height
                    text: qsTr("Age")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    font.bold: true
                }
                Label {
                    width: root.statusWidth
                    height: parent.height
                    text: qsTr("State")
                    color: "#94a3b8"
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    font.bold: true
                }
            }
        }

        ListView {
            id: signalList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.chartVisible
            clip: true
            model: root.dataModel
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            spacing: 1

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Rectangle {
                id: signalRow
                required property int index
                required property string signalName
                required property string formattedValue
                required property string unit
                required property string sourceName
                required property string ageText
                required property bool stale
                required property bool selected

                width: signalList.width
                height: 42
                color: index % 2 === 0 ? "#101720" : "#0d131b"

                Row {
                    anchors.fill: parent

                    CheckBox {
                        width: root.selectWidth
                        height: parent.height
                        checked: signalRow.selected
                        onClicked: root.dataModel.setSelected(signalRow.signalName, checked)
                    }

                    Label {
                        width: parent.width - root.selectWidth - root.valueWidth - root.unitWidth
                               - root.sourceWidth - root.ageWidth - root.statusWidth
                        height: parent.height
                        leftPadding: 10
                        text: signalRow.signalName
                        color: signalRow.stale ? "#64748b" : "#e2e8f0"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.family: "monospace"
                        font.pixelSize: 15
                    }

                    Label {
                        width: root.valueWidth
                        height: parent.height
                        rightPadding: 12
                        text: signalRow.formattedValue
                        color: signalRow.stale ? "#64748b" : "#f8fafc"
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        font.family: "monospace"
                        font.pixelSize: 16
                        font.bold: true
                    }

                    Label {
                        width: root.unitWidth
                        height: parent.height
                        leftPadding: 8
                        text: signalRow.unit
                        color: "#94a3b8"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    Label {
                        width: root.sourceWidth
                        height: parent.height
                        text: signalRow.sourceName
                        color: "#94a3b8"
                        verticalAlignment: Text.AlignVCenter
                    }

                    Label {
                        width: root.ageWidth
                        height: parent.height
                        text: signalRow.ageText
                        color: signalRow.stale ? "#fb7185" : "#94a3b8"
                        verticalAlignment: Text.AlignVCenter
                    }

                    Item {
                        width: root.statusWidth
                        height: parent.height

                        Rectangle {
                            anchors.centerIn: parent
                            width: 60
                            height: 24
                            radius: 12
                            color: signalRow.stale ? "#4c1d2a" : "#12372a"

                            Label {
                                anchors.centerIn: parent
                                text: signalRow.stale ? qsTr("STALE") : qsTr("LIVE")
                                color: signalRow.stale ? "#fda4af" : "#86efac"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: signalList.count === 0
                text: filterField.text.length > 0
                      ? qsTr("No signals match the filter")
                      : qsTr("Waiting for signal definitions…")
                color: "#64748b"
                font.pixelSize: 18
            }
        }

        HistoryChart {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.chartVisible
            historyStore: root.historyStore
        }
    }
}
