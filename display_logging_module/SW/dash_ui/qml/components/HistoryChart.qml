pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var historyStore
    readonly property var colors: ["#38bdf8", "#facc15", "#fb7185", "#a78bfa"]

    color: "#0d131b"
    radius: 8
    border.color: "#334155"

    function formatValue(value) {
        const magnitude = Math.abs(value)
        return Number(value).toFixed(magnitude >= 1000 ? 0 : magnitude >= 100 ? 1 : 2)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Live Signal History"); color: "#f1f5f9"; font.pixelSize: 20; font.bold: true }
            Label { text: qsTr("Each trace is auto-scaled independently"); color: "#64748b"; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Window"); color: "#94a3b8" }
            ComboBox {
                model: [5, 10, 30, 60, 120]
                currentIndex: Math.max(0, model.indexOf(root.historyStore.windowSeconds))
                displayText: qsTr("%1 s").arg(currentValue)
                onActivated: root.historyStore.windowSeconds = currentValue
            }
            Button { text: qsTr("Clear"); onClicked: root.historyStore.clear() }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 18
            Repeater {
                model: root.historyStore.series
                delegate: RowLayout {
                    id: legend
                    required property var modelData
                    required property int index
                    spacing: 6
                    Rectangle {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        radius: 6
                        color: root.colors[legend.index % root.colors.length]
                    }
                    Label {
                        readonly property var points: legend.modelData.points
                        readonly property real latest: points.length > 0 ? points[points.length - 1].y : NaN
                        text: legend.modelData.name + (isNaN(latest) ? "" : "  " + root.formatValue(latest) + " " + legend.modelData.unit)
                        color: "#cbd5e1"
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                }
            }
        }

        Canvas {
            id: chart
            Layout.fillWidth: true
            Layout.fillHeight: true
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Connections {
                target: root.historyStore
                function onSeriesChanged() { chart.requestPaint() }
                function onWindowSecondsChanged() { chart.requestPaint() }
            }

            onPaint: {
                const context = getContext("2d")
                context.reset()
                const left = 46, right = 16, top = 12, bottom = 30
                const plotWidth = Math.max(1, width - left - right)
                const plotHeight = Math.max(1, height - top - bottom)
                context.strokeStyle = "#263442"
                context.lineWidth = 1
                for (let i = 0; i <= 5; ++i) {
                    const x = left + plotWidth * i / 5
                    context.beginPath(); context.moveTo(x, top); context.lineTo(x, top + plotHeight); context.stroke()
                    const y = top + plotHeight * i / 5
                    context.beginPath(); context.moveTo(left, y); context.lineTo(left + plotWidth, y); context.stroke()
                }
                context.fillStyle = "#64748b"
                context.font = "12px sans-serif"
                context.fillText("-" + root.historyStore.windowSeconds + " s", left, height - 8)
                context.fillText("now", left + plotWidth - 24, height - 8)

                const allSeries = root.historyStore.series
                for (let seriesIndex = 0; seriesIndex < allSeries.length; ++seriesIndex) {
                    const points = allSeries[seriesIndex].points
                    if (points.length < 2) continue
                    let minimum = points[0].y, maximum = points[0].y
                    for (let p = 1; p < points.length; ++p) {
                        minimum = Math.min(minimum, points[p].y)
                        maximum = Math.max(maximum, points[p].y)
                    }
                    if (Math.abs(maximum - minimum) < 1e-9) {
                        const padding = Math.max(1, Math.abs(maximum) * 0.05)
                        minimum -= padding; maximum += padding
                    } else {
                        const padding = (maximum - minimum) * 0.08
                        minimum -= padding; maximum += padding
                    }
                    context.strokeStyle = root.colors[seriesIndex % root.colors.length]
                    context.lineWidth = 2
                    context.beginPath()
                    for (let pointIndex = 0; pointIndex < points.length; ++pointIndex) {
                        const point = points[pointIndex]
                        const x = left + (point.x + root.historyStore.windowSeconds) / root.historyStore.windowSeconds * plotWidth
                        const y = top + (maximum - point.y) / (maximum - minimum) * plotHeight
                        if (pointIndex === 0) context.moveTo(x, y); else context.lineTo(x, y)
                    }
                    context.stroke()
                }
                if (allSeries.length === 0) {
                    context.fillStyle = "#64748b"; context.font = "18px sans-serif"; context.textAlign = "center"
                    context.fillText("Select signals in the list", width / 2, height / 2)
                    context.textAlign = "left"
                }
            }
        }
    }
}
