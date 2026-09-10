import QtQuick.Controls
import QtQuick
import ".."

Button {
    id: btn

    property bool  secondary: false
    property color backgroundDefaultColor: secondary ? Theme.surfaceAlt : Theme.accent
    property color contentItemTextColor: secondary ? Theme.textPrimary
                                                   : Theme.onColor(backgroundDefaultColor)
    property int   fontSize: 20
    property alias radius: bg.radius

    hoverEnabled: true
    focusPolicy: Qt.NoFocus

    scale: down ? 0.95 : (hovered ? 1.02 : 1.0)
    Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutBack } }

    contentItem: Text {
        text: btn.text
        color: btn.contentItemTextColor
        font {
            pixelSize: Globals.sp(btn.fontSize)
            weight: Font.DemiBold
            letterSpacing: 0.4
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: btn.down ? 0.7 : 1.0
        Behavior on opacity { NumberAnimation { duration: 100 } }
    }

    background: Rectangle {
        id: bg
        radius: 10
        clip: true
        color: btn.down ? Qt.darker(btn.backgroundDefaultColor, 1.4)
                        : btn.backgroundDefaultColor
        Behavior on color { ColorAnimation { duration: 110 } }

        border.width: 1
        border.color: btn.down    ? "transparent"
                    : btn.hovered ? Theme.glossHi
                    :               Qt.alpha(Theme.glossHi, 0.11)
        Behavior on border.color { ColorAnimation { duration: 200 } }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            opacity: btn.hovered && !btn.down ? 1.0 : 0.0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.glossLo }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        Rectangle {
            id: shine
            width: parent.width * 0.45
            height: parent.height * 3
            x: -width
            y: -(parent.height)
            opacity: 0
            rotation: -18
            transformOrigin: Item.Center
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.glossLo }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        SequentialAnimation {
            id: shineAnim
            PropertyAction { target: shine; property: "x"; value: -shine.width }
            PropertyAction { target: shine; property: "opacity"; value: 1.0 }
            NumberAnimation {
                target: shine
                property: "x"
                to: bg.width + shine.width
                duration: 520
                easing.type: Easing.OutCubic
            }
            PropertyAction { target: shine; property: "opacity"; value: 0 }
        }
    }

    onHoveredChanged: {
        if (hovered) shineAnim.restart()
    }
}