import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property string sourceName: "No data source"
    required property var signalProvider
    required property var presentationProvider

    Rectangle {
        anchors.fill: parent
        color: "#090c10"
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 24

        AnalogGauge {
            Layout.preferredWidth: 610
            Layout.fillHeight: true
            signalName: "ECM.rpm"
            signalProvider: root.signalProvider
            presentationProvider: root.presentationProvider
        }

        AnalogGauge {
            Layout.preferredWidth: 520
            Layout.fillHeight: true
            signalName: "ECM.clt"
            signalProvider: root.signalProvider
            presentationProvider: root.presentationProvider
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: root.sourceName
                color: "#7dd3fc"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignRight
            }

            DigitalGauge {
                Layout.fillWidth: true
                Layout.fillHeight: true
                signalName: "ECM.batt"
                signalProvider: root.signalProvider
                presentationProvider: root.presentationProvider
            }

            DigitalGauge {
                Layout.fillWidth: true
                Layout.fillHeight: true
                signalName: "ECM.afr1_old"
                signalProvider: root.signalProvider
                presentationProvider: root.presentationProvider
            }

            DigitalGauge {
                Layout.fillWidth: true
                Layout.fillHeight: true
                signalName: "ECM.fuel_pct"
                signalProvider: root.signalProvider
                presentationProvider: root.presentationProvider
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("UP/DOWN pages    MENU options    ACK warning")
                color: "#64748b"
                font.pixelSize: 14
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
