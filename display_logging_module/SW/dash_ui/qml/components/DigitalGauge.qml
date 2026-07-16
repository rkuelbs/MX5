import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string signalName
    required property var signalProvider
    required property var presentationProvider

    readonly property var signal: root.signalProvider
                                  ? root.signalProvider.channel(root.signalName) : null
    readonly property var presentation: root.presentationProvider
                                        ? root.presentationProvider.presentation(root.signalName) : null
    readonly property real numericValue: root.signal && root.signal.valid
                                         ? Number(root.signal.value) : 0
    readonly property string zoneColor: {
        const p = root.presentation
        if (!p || !root.signal || !root.signal.valid)
            return "#334155"
        if ((p.lowWarningValid && root.numericValue <= p.lowWarning)
                || (p.highWarningValid && root.numericValue >= p.highWarning))
            return "#ef4444"
        if ((p.lowCautionValid && root.numericValue <= p.lowCaution)
                || (p.highCautionValid && root.numericValue >= p.highCaution))
            return "#facc15"
        return "#38bdf8"
    }

    radius: 10
    color: "#111923"
    border.color: root.zoneColor
    border.width: 3
    opacity: root.signal && !root.signal.stale ? 1.0 : 0.55

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        text: root.presentation ? root.presentation.displayName : root.signalName
        color: "#cbd5e1"
        font.pixelSize: 18
        font.weight: Font.DemiBold
    }

    Label {
        anchors.right: unitLabel.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.signal && root.signal.valid
              ? root.numericValue.toFixed(root.presentation ? root.presentation.precision : 0)
              : "--"
        color: "#f8fafc"
        font.pixelSize: 36
        font.bold: true
        font.family: "monospace"
    }

    Label {
        id: unitLabel
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        width: 70
        text: root.signal ? root.signal.unit : ""
        color: "#94a3b8"
        font.pixelSize: 16
    }
}
