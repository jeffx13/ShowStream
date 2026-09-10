import QtQuick
import QtQuick.Controls
import ".."

ComboBox {
    id: comboBox

    property color currentIndexColor: Theme.surfaceAlt
    property int fontSize: 20

    property string text: "text"
    property string placeholderText: ""

    textRole: comboBox.text && comboBox.text.length > 0 ? comboBox.text : ""

    // Reads index/model/modelData from the delegate context rather than declaring them required:
    // one delegate serves ListModels, QStringLists and JS arrays, which resolve differently.
    delegate: ItemDelegate {
        id: itemDel
        width: comboBox.width
        height: 40
        enabled: !model.disabled
        opacity: enabled ? 1.0 : 0.5

        readonly property bool isCurrent: index === comboBox.currentIndex

        contentItem: Text {
            leftPadding: 10
            rightPadding: 22
            text: (comboBox.text.length === 0 || typeof model[comboBox.text] === "undefined")
                  ? modelData : model[comboBox.text]
            color: comboBox.highlightedIndex === index ? Theme.onColor(Theme.accent)
                 : itemDel.isCurrent ? Theme.textAccent
                 : Theme.textPrimary
            font.weight: itemDel.isCurrent ? Font.Medium : Font.Normal
            elide: Text.ElideRight
            font.pixelSize: Globals.sp(comboBox.fontSize)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }

        background: Rectangle {
            color: {
                if (comboBox.highlightedIndex === index) return Theme.accent
                return itemDel.isCurrent ? comboBox.currentIndexColor : Theme.surface
            }
            radius: 8

            Rectangle {
                visible: itemDel.isCurrent
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 2 }
                width: 3; height: parent.height - 14; radius: 1.5
                color: Theme.accent
            }
            AppIcon {
                visible: itemDel.isCurrent
                anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 9 }
                name: "check"
                size: 16
                color: comboBox.highlightedIndex === index ? Theme.onColor(Theme.accent) : Theme.accent
            }
        }
    }

    indicator: AppIcon {
        x: comboBox.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 16
        color: comboBox.down ? Theme.textAccent : Theme.textMuted
    }

    contentItem: Text {
        width: comboBox.background.width - 30
        height: comboBox.background.height
        anchors.verticalCenter: parent.verticalCenter
        x: 12
        text: comboBox.currentIndex < 0 && comboBox.placeholderText.length > 0
              ? comboBox.placeholderText : comboBox.displayText
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Globals.sp(comboBox.fontSize)
        color: comboBox.currentIndex < 0 && comboBox.placeholderText.length > 0
               ? Theme.textMuted : Theme.textPrimary
    }

    background: FieldBackground {
        implicitWidth: 102
        implicitHeight: 41
        radius: 12
        focused: comboBox.activeFocus
        hovered: comboBox.hovered
    }

    popup: Popup {
        parent: comboBox
        x: 0
        y: comboBox.height + 4
        width: comboBox.width
        implicitHeight: contentItem.implicitHeight + 8
        padding: 4
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        enter: Transition {
            ParallelAnimation {
                NumberAnimation {
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 150
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    property: "scale"
                    from: 0.95
                    to: 1.0
                    duration: 150
                    easing.type: Easing.OutCubic
                }
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 100
            }
        }

        contentItem: ListView {
            implicitHeight: contentHeight
            model: comboBox.popup.visible ? comboBox.delegateModel : null
            clip: true
            currentIndex: comboBox.highlightedIndex
            spacing: 2
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            radius: 12

            Rectangle {
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    topMargin: 1
                    leftMargin: 10
                    rightMargin: 10
                }
                height: 1
                color: Theme.border
            }
        }
    }
}