import QtQuick
import QtQuick.Layouts
import ".."

GridLayout {
    id: root

    property alias label:      labelText.text
    property alias sublabel:   subText.text
    property alias from:       slider.from
    property alias to:         slider.to
    property alias stepSize:   slider.stepSize
    property alias value:      slider.value
    property alias unitSuffix: slider.unitSuffix
    property alias decimals:   slider.decimals
    property bool  stacked: false
    // The player panel floats over video and needs its own foreground, not the page palette's.
    property color labelColor: Theme.textSecondary
    signal moved(real v)

    Layout.fillWidth: true
    columns: stacked ? 1 : 2
    columnSpacing: 12
    rowSpacing: stacked ? 2 : 0

    ColumnLayout {
        spacing: 0
        Text {
            id: labelText
            color: root.labelColor
            font.pixelSize: Globals.sp(20)
        }
        Text {
            id: subText
            visible: text.length > 0
            color: Theme.textMuted
            font.pixelSize: Globals.sp(15)
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
    }

    AppSlider {
        id: slider
        Layout.fillWidth: true
        onMoved: root.moved(value)
    }
}
