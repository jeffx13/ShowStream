pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

// A SubDL result only joins the player's track list once it is picked, so this keeps its own list.
Item {
    id: root

    function refresh() {
        subQueryField.offerShowName()
        App.subtitleSearch.searchIfNew(subQueryField.text)
    }

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppTextField {
                id: subQueryField
                Layout.fillWidth: true
                placeholderText: qsTr("Search subtitles by title")
                showClearButton: true
                fontSize: 18
                onAccepted: App.subtitleSearch.search(text)

                // currentShowName() has no notifier, so refill on entry rather than bind.
                property string prefilled: ""
                function offerShowName() {
                    if (text !== "" && text !== prefilled) return
                    prefilled = App.playlist.currentShowName()
                    text = prefilled
                }
                Component.onCompleted: offerShowName()
            }

            AppButton {
                text: qsTr("Search")
                radius: 8
                fontSize: 18
                enabled: !App.subtitleSearch.isLoading
                onClicked: App.subtitleSearch.search(subQueryField.text)
            }
        }

        Flow {
            id: subLangs
            Layout.fillWidth: true
            spacing: 6

            readonly property var codes: ["EN", "ES", "FR", "DE", "PT", "AR", "ZH", "JA", "KO"]
            property var chosen: []

            function load() {
                const saved = App.settings.value("subtitles/subdlLanguages", "EN")
                chosen = saved.split(",").filter(c => c !== "")
            }
            function toggle(code) {
                let next = chosen.slice()
                const at = next.indexOf(code)
                if (at >= 0) next.splice(at, 1)
                else next.push(code)
                if (next.length === 0) return   // never leave every language off
                chosen = next
                App.settings.setValue("subtitles/subdlLanguages", next.join(","))
                if (!App.subtitleSearch.isLoading)
                    App.subtitleSearch.search(subQueryField.text)
            }
            Component.onCompleted: load()

            Repeater {
                model: subLangs.codes
                delegate: Chip {
                    id: langChip
                    required property string modelData
                    label: modelData
                    filled: subLangs.chosen.indexOf(modelData) >= 0
                    enabled: !App.subtitleSearch.isLoading
                    opacity: enabled ? 1.0 : 0.5
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: subLangs.toggle(langChip.modelData) }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: App.subtitleSearch.count === 0
            wrapMode: Text.Wrap
            color: Theme.onOverlayDim
            font.pixelSize: Globals.sp(18)
            text: App.subtitleSearch.isLoading  ? qsTr("Searching...")
                : App.subtitleSearch.query === "" ? qsTr("Search SubDL for a subtitle to use.")
                : qsTr("Nothing found.")
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: App.subtitleSearch
            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: subResult
                required property string displayName
                required property string release
                required property string language
                required property string author
                required property string episodeLabel
                required property var    tags
                required property bool   hearingImpaired
                required property int    slot
                required property bool   fetching
                required property int    index

                readonly property var chips: {
                    let c = []
                    if (episodeLabel !== "") c.push(episodeLabel)
                    if (language !== "") c.push(language)
                    for (const t of tags) c.push(t)
                    if (hearingImpaired) c.push("SDH")
                    return c
                }

                width: ListView.view.width
                implicitHeight: subCol.implicitHeight + 20
                radius: 10
                color: subResult.slot > 0 ? Qt.alpha(Theme.accent, 0.16)
                     : subHover.hovered   ? Qt.alpha(Theme.onOverlay, 0.07)
                                          : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }

                Rectangle {
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: 3
                    radius: 2
                    color: Theme.accent
                    visible: subResult.slot > 0
                }

                ColumnLayout {
                    id: subCol
                    anchors {
                        left: parent.left; right: parent.right; top: parent.top
                        leftMargin: 12; rightMargin: 12; topMargin: 10
                    }
                    spacing: 7

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: subResult.displayName
                            color: Theme.onOverlay
                            font.pixelSize: Globals.sp(17)
                            font.bold: subResult.slot > 0
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: subResult.slot > 0
                            text: subResult.slot === 1 ? qsTr("PRIMARY") : qsTr("SECONDARY")
                            color: Theme.accent
                            font.pixelSize: Globals.sp(12)
                            font.bold: true
                        }

                        // Tapping the row takes slot 1; this is the only way to slot 2.
                        Chip {
                            visible: subHover.hovered && subResult.slot !== 2
                            label: "2"
                            filled: subSecondHover.hovered
                            HoverHandler { id: subSecondHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: App.subtitleSearch.use(subResult.index, true) }
                        }

                        AppSpinner {
                            visible: subResult.fetching
                            running: subResult.fetching
                            radius: 6
                            dotSize: 3
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: subResult.chips
                            delegate: Chip {
                                required property string modelData
                                label: modelData
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            visible: subResult.author !== ""
                            text: subResult.author
                            color: Theme.onOverlayDim
                            font.pixelSize: Globals.sp(13)
                            elide: Text.ElideRight
                        }
                    }
                }

                HoverHandler { id: subHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: !subSecondHover.hovered   // the "2" chip wins its own taps
                    onTapped: App.subtitleSearch.use(subResult.index)
                }
            }
        }
    }
}
