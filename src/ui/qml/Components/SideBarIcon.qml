pragma ComponentBehavior: Bound
import QtQuick
import Qt5Compat.GraphicalEffects
import ".."

Item {
    id: root

    property string glyph: ""
    property bool   active: false
    property int    size: 34

    implicitWidth: size
    implicitHeight: size

    readonly property color cloudColor: active ? Theme.accent : Qt.alpha(Theme.textMuted, 0.55)
    readonly property color glyphColor: active ? Theme.onColor(Theme.accent) : Theme.textSecondary

    scale: active ? 1.06 : 1.0
    Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }

    // One disc for both sun and moon, so day/night is a single rise and colour change.
    Rectangle {
        width: root.size * 0.36
        height: width
        radius: width / 2
        x: root.size * 0.58
        y: root.active ? root.size * 0.03 : root.size * 0.10
        color: root.active ? Theme.warning : Qt.alpha(Theme.textSecondary, 0.50)
        Behavior on y     { NumberAnimation { duration: 340; easing.type: Easing.OutBack } }
        Behavior on color { ColorAnimation  { duration: 260 } }
    }

    Image {
        id: cloud
        anchors.fill: parent
        source: "qrc:/AoNami/resources/icons/cloud-badge.svg"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }
    ColorOverlay {
        anchors.fill: cloud
        source: cloud
        color: root.cloudColor
        Behavior on color { ColorAnimation { duration: 260 } }
    }

    AppIcon {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: Math.round(root.size * 0.05)
        name: root.glyph
        size: Math.round(root.size * 0.44)
        color: root.glyphColor
        Behavior on color { ColorAnimation { duration: 260 } }
    }
}
