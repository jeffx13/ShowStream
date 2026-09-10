pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AoNami
import QtQuick.Dialogs
import "../Components"
import ".."

Page {
    background: Rectangle { color: "transparent" }

    header: Item {
        height: 48
        Text {
            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
            text: qsTr("Settings"); color: Theme.textPrimary; font { pixelSize: Globals.sp(24); bold: true }
        }
    }

    ColorDialog {
        id: accentDialog
        // QVariant won't convert color->string, so stringify or the accent stays the theme default.
        onAccepted: App.settings.accentColor = selectedColor.toString()
    }

    Component {
        id: themeSwatch
        Rectangle {
            id: swatch
            required property var modelData
            readonly property var colours: Theme.palettes[swatch.modelData.name]
            readonly property bool active: App.settings.themeName === swatch.modelData.name

            width: 132; height: 78; radius: 10
            gradient: Gradient {
                GradientStop { position: 0.0; color: swatch.colours.background }
                GradientStop { position: 1.0; color: swatch.colours.bgBottom }
            }
            border.color: swatch.active ? Theme.accent : swatch.colours.border
            border.width: swatch.active ? 2 : 1

            Column {
                anchors { fill: parent; margins: 10 }
                spacing: 8
                Row {
                    spacing: 6
                    Rectangle {
                        width: 26; height: 14; radius: 4
                        color: swatch.colours.surface
                        border.color: swatch.colours.border
                        border.width: 1
                    }
                    Rectangle { width: 42; height: 14; radius: 4; color: swatch.colours.accent }
                }
                Text {
                    text: swatch.modelData.label
                    color: swatch.colours.textPrimary
                    font.pixelSize: Globals.sp(15)
                }
            }
            AppIcon {
                visible: swatch.active
                anchors { top: parent.top; right: parent.right; topMargin: 6; rightMargin: 8 }
                name: "check"; size: 16; color: Theme.accent
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: App.settings.themeName = swatch.modelData.name
            }
        }
    }


    component SettingsCard: Card {
        property alias title: titleText.text
        default property alias content: cardCol.children
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 24
        radius: 10

        ColumnLayout {
            id: cardCol
            anchors { fill: parent; margins: 12 }
            spacing: 10
            Text { id: titleText; color: Theme.accent; font { pixelSize: Globals.sp(20); bold: true } }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: rootCol.implicitHeight + 32
        clip: true; boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: rootCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
            spacing: 12

            SettingsCard {
                title: qsTr("Appearance")

                Text { text: qsTr("Theme"); color: Theme.textSecondary; font.pixelSize: Globals.sp(20) }

                Text { text: qsTr("Dark"); color: Theme.textMuted; font.pixelSize: Globals.sp(17) }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater { model: Theme.swatches(false); delegate: themeSwatch }
                }

                Text { text: qsTr("Light"); color: Theme.textMuted; font.pixelSize: Globals.sp(17) }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater { model: Theme.swatches(true); delegate: themeSwatch }
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Text { text: qsTr("Accent"); color: Theme.textSecondary; font.pixelSize: Globals.sp(20) }
                    Rectangle { Layout.preferredWidth: 28; Layout.preferredHeight: 28; radius: 6; color: Theme.accent; border.color: Theme.border; border.width: 1 }
                    Item { Layout.fillWidth: true }
                    AppButton { text: qsTr("Change"); onClicked: { accentDialog.selectedColor = Theme.accent; accentDialog.open() } }
                    AppButton { text: qsTr("Reset"); secondary: true; onClicked: App.settings.accentColor = "" }
                }

                LabeledSlider {
                    label: qsTr("UI Scale")
                    sublabel: qsTr("Size of text and controls across the app")
                    from: 0.8; to: 1.4; stepSize: 0.05
                    unitSuffix: "x"; decimals: 2
                    value: App.settings.uiScale
                    onMoved: (v) => App.settings.uiScale = v
                }
            }

            SettingsCard {
                title: qsTr("General")

                LabeledRow {
                    label: qsTr("MPV Logs")
                    AppSwitch { checked: App.settings.mpvLogEnabled; onToggled: App.settings.mpvLogEnabled = checked }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton { text: qsTr("Clear Logs"); onClicked: App.logList.clear() }
                    Item { Layout.fillWidth: true }
                }
            }

            SettingsCard {
                title: qsTr("Network")

                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: qsTr("Proxy"); color: Theme.textSecondary; font.pixelSize: Globals.sp(20) }
                    AppTextField {
                        id: proxyField; text: App.settings.proxy
                        placeholderText: qsTr("http://127.0.0.1:7890")
                        Layout.fillWidth: true
                        onEditingFinished: App.settings.proxy = text
                    }
                    AppButton { text: qsTr("Apply"); onClicked: App.settings.proxy = proxyField.text }
                    AppButton { text: qsTr("Clear"); secondary: true; onClicked: { proxyField.text = ""; App.settings.proxy = "" } }
                }
            }

            SettingsCard {
                title: qsTr("Player")

                LabeledRow {
                    label: qsTr("Use yt-dlp")
                    AppSwitch { checked: App.settings.mpvYtdlEnabled; onToggled: App.settings.mpvYtdlEnabled = checked }
                }

                LabeledRow {
                    label: qsTr("Prefer Dubbed Audio")
                    sublabel: qsTr("Try dub servers first (off = subbed)")
                    AppSwitch { checked: App.settings.preferDub; onToggled: App.settings.preferDub = checked }
                }

                LabeledSlider {
                    label: qsTr("Mark Watched At")
                    sublabel: qsTr("Episode counts as watched past this much")
                    from: 0; to: 100; stepSize: 5
                    unitSuffix: "%"
                    value: App.settings.watchedPercent
                    onMoved: (v) => App.settings.watchedPercent = v
                }

                Flow {
                    Layout.fillWidth: true; spacing: 6
                    AppButton { text: qsTr("mpv folder"); secondary: true; onClicked: Qt.openUrlExternally("file:///" + App.settings.appDir + "/mpv") }
                    AppButton { text: qsTr("mpv.conf"); secondary: true; onClicked: Qt.openUrlExternally("file:///" + App.settings.appDir + "/mpv/mpv.conf") }
                    AppButton { text: qsTr("settings.ini"); secondary: true; onClicked: Qt.openUrlExternally(App.settings.path) }
                }
            }

            SettingsCard {
                title: qsTr("Danmaku")

                LabeledRow {
                    label: qsTr("Show Danmaku")
                    sublabel: qsTr("Bullet comments, where the provider has them")
                    AppSwitch { checked: App.settings.danmakuEnabled; onToggled: App.settings.danmakuEnabled = checked }
                }

                LabeledSlider {
                    label: qsTr("Opacity")
                    from: 10; to: 100; stepSize: 5
                    unitSuffix: "%"
                    value: App.settings.danmakuOpacity
                    onMoved: (v) => App.settings.danmakuOpacity = v
                }

                LabeledSlider {
                    label: qsTr("Font Size")
                    from: 50; to: 200; stepSize: 10
                    unitSuffix: "%"
                    value: App.settings.danmakuFontScale
                    onMoved: (v) => App.settings.danmakuFontScale = v
                }

                LabeledSlider {
                    label: qsTr("Speed")
                    from: 25; to: 400; stepSize: 25
                    unitSuffix: "%"
                    value: App.settings.danmakuSpeed
                    onMoved: (v) => App.settings.danmakuSpeed = v
                }

                LabeledSlider {
                    label: qsTr("Screen Area")
                    sublabel: qsTr("How far down the frame comments may go")
                    from: 10; to: 100; stepSize: 5
                    unitSuffix: "%"
                    value: App.settings.danmakuArea
                    onMoved: (v) => App.settings.danmakuArea = v
                }

                LabeledSlider {
                    label: qsTr("Max On Screen")
                    sublabel: qsTr("0 for no limit")
                    from: 0; to: 200; stepSize: 10
                    value: App.settings.danmakuMaxOnScreen
                    onMoved: (v) => App.settings.danmakuMaxOnScreen = v
                }

                LabeledSlider {
                    label: qsTr("Hide Spam")
                    sublabel: qsTr("Drop comments rated below this (0 keeps all)")
                    from: 0; to: 11; stepSize: 1
                    value: App.settings.danmakuMinWeight
                    onMoved: (v) => App.settings.danmakuMinWeight = v
                }

                LabeledRow {
                    label: qsTr("Outline")
                    AppComboBox {
                        Layout.preferredWidth: 160
                        model: [qsTr("None"), qsTr("Outline"), qsTr("Outline + Shadow")]
                        currentIndex: App.settings.danmakuOutline
                        onActivated: App.settings.danmakuOutline = currentIndex
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("Hide comment types")
                    color: Theme.textSecondary
                    font.pixelSize: Globals.sp(20)
                }

                Flow {
                    Layout.fillWidth: true; spacing: 14

                    MiniToggle { label: qsTr("Scrolling"); checked: App.settings.danmakuBlockScroll; onToggled: App.settings.danmakuBlockScroll = checked }
                    MiniToggle { label: qsTr("Top");       checked: App.settings.danmakuBlockTop;    onToggled: App.settings.danmakuBlockTop = checked }
                    MiniToggle { label: qsTr("Bottom");    checked: App.settings.danmakuBlockBottom; onToggled: App.settings.danmakuBlockBottom = checked }
                    MiniToggle { label: qsTr("Colour");    checked: App.settings.danmakuBlockColour; onToggled: App.settings.danmakuBlockColour = checked }
                    MiniToggle { label: qsTr("Repeats");   checked: App.settings.danmakuBlockRepeat; onToggled: App.settings.danmakuBlockRepeat = checked }
                }

                Flow {
                    Layout.fillWidth: true; spacing: 6
                    AppButton {
                        text: qsTr("Reset Appearance")
                        secondary: true
                        onClicked: App.settings.resetDanmakuAppearance()
                    }
                }
            }

            SettingsCard {
                title: qsTr("Downloads")

                LabeledRow {
                    label: qsTr("Concurrent Downloads")
                    AppSpinBox {
                        from: 1; to: 8
                        value: App.downloads.maxDownloads
                        onValueModified: App.downloads.maxDownloads = value
                    }
                }

                LabeledRow {
                    label: qsTr("Speed Limit")
                    AppTextField {
                        placeholderText: qsTr("e.g. 5M  (blank = unlimited)")
                        text: App.settings.maxSpeed
                        implicitWidth: 200
                        onEditingFinished: App.settings.maxSpeed = text
                    }
                }
            }

            SettingsCard {
                title: qsTr("Integrations")

                LabeledRow {
                    label: qsTr("Discord Rich Presence")
                    sublabel: qsTr("Show what you're watching on Discord")
                    AppSwitch { checked: App.settings.discordEnabled; onToggled: App.settings.discordEnabled = checked }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}