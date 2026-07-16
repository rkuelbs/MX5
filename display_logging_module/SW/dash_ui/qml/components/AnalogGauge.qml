pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    required property string signalName
    required property var signalProvider
    required property var presentationProvider
    property real startAngle: -135
    property real endAngle: 135
    property url backgroundSource
    property url needleSource
    property real needleLength: root.dialSize * 0.34
    property real needleWidth: root.dialSize * 0.08

    readonly property var signal: root.signalProvider
                                  ? root.signalProvider.channel(root.signalName) : null
    readonly property var presentation: root.presentationProvider
                                        ? root.presentationProvider.presentation(root.signalName) : null
    readonly property real minimumValue: root.presentation ? root.presentation.gaugeMinimum : 0
    readonly property real maximumValue: root.presentation ? root.presentation.gaugeMaximum : 100
    readonly property real minorStep: root.presentation ? root.presentation.minorStep : 10
    readonly property real majorStep: root.presentation ? root.presentation.majorStep : 20
    readonly property real numericValue: root.signal && root.signal.valid
                                         ? Number(root.signal.value) : root.minimumValue
    readonly property real boundedValue: Math.max(root.minimumValue,
                                                   Math.min(root.maximumValue, root.numericValue))
    readonly property real needleAngle: root.startAngle
                                        + (root.boundedValue - root.minimumValue)
                                        / (root.maximumValue - root.minimumValue)
                                        * (root.endAngle - root.startAngle)
    readonly property real dialSize: Math.min(width, height)
    readonly property int tickCount: root.minorStep > 0
                                     ? Math.floor((root.maximumValue - root.minimumValue)
                                                  / root.minorStep + 0.5) + 1 : 0

    function thresholdColor(value) {
        const p = root.presentation
        if (!p)
            return "#f1f5f9"
        if ((p.lowWarningValid && value <= p.lowWarning)
                || (p.highWarningValid && value >= p.highWarning))
            return "#ef4444"
        if ((p.lowCautionValid && value <= p.lowCaution)
                || (p.highCautionValid && value >= p.highCaution))
            return "#facc15"
        return "#f1f5f9"
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.dialSize
        height: width
        radius: width / 2
        color: "#101720"
        border.color: "#334155"
        border.width: 3
    }

    Image {
        anchors.centerIn: parent
        width: root.dialSize
        height: width
        source: root.backgroundSource
        visible: root.backgroundSource.toString().length > 0
        fillMode: Image.PreserveAspectFit
        cache: true
    }

    Repeater {
        model: root.tickCount

        delegate: Item {
            id: tick
            required property int index
            readonly property real tickValue: root.minimumValue + tick.index * root.minorStep
            readonly property bool major: Math.abs(
                (tick.tickValue - root.minimumValue) / root.majorStep
                - Math.round((tick.tickValue - root.minimumValue) / root.majorStep)) < 0.001

            anchors.centerIn: parent
            width: root.dialSize
            height: width
            rotation: root.startAngle + tick.index / Math.max(1, root.tickCount - 1)
                      * (root.endAngle - root.startAngle)

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: root.dialSize * 0.055
                width: tick.major ? 5 : 3
                height: tick.major ? root.dialSize * 0.075 : root.dialSize * 0.045
                radius: width / 2
                color: root.thresholdColor(tick.tickValue)
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                y: root.dialSize * 0.145
                visible: tick.major
                text: Number(tick.tickValue).toFixed(0)
                color: root.thresholdColor(tick.tickValue)
                font.pixelSize: Math.max(11, root.dialSize * 0.045)
                font.bold: true
                rotation: -tick.rotation
            }
        }
    }

    Item {
        anchors.centerIn: parent
        width: root.dialSize
        height: width
        rotation: root.needleAngle

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.dialSize * 0.5 - root.needleLength
            width: root.needleWidth
            height: root.needleLength
            source: root.needleSource
            visible: root.needleSource.toString().length > 0
            fillMode: Image.PreserveAspectFit
            cache: true
        }

        Rectangle {
            visible: root.needleSource.toString().length === 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: root.dialSize * 0.5 - height
            width: Math.max(5, root.dialSize * 0.018)
            height: root.dialSize * 0.32
            radius: width / 2
            color: "#fb7185"
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.dialSize * 0.075
        height: width
        radius: width / 2
        color: "#e2e8f0"
        border.color: "#475569"
        border.width: 2
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.height * 0.61
        spacing: 1

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.signal && root.signal.valid
                  ? Number(root.signal.value).toFixed(root.presentation ? root.presentation.precision : 0)
                  : "--"
            color: root.signal && !root.signal.stale ? "#f8fafc" : "#64748b"
            font.pixelSize: root.dialSize * 0.105
            font.bold: true
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.signal ? root.signal.unit : ""
            color: "#94a3b8"
            font.pixelSize: root.dialSize * 0.038
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.presentation ? root.presentation.displayName : root.signalName
            color: "#cbd5e1"
            font.pixelSize: root.dialSize * 0.046
            font.weight: Font.DemiBold
        }
    }
}
