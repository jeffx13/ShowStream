pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "./../Components"
import QtQuick.Layouts
import AoNami
import ".."
import Qt5Compat.GraphicalEffects

Item {
    id: infoPage
    focus: true
    readonly property var currentShow: App.show

    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }

    component MetaChip: Rectangle {
        id: chip
        property string iconName
        property string chipValue
        property bool   pill: true
        property color  iconColor:  Theme.textSecondary
        property color  valueColor: Theme.textSecondary

        visible: chipValue.length > 0
        implicitWidth: chipRow.implicitWidth + 16
        height: pill ? 30 : 26
        radius: pill ? height / 2 : 6
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 5
            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: chip.iconName
                size: chip.pill ? 16 : 14
                color: chip.iconColor
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.chipValue
                color: chip.valueColor
                font.pixelSize: Globals.sp(20)
            }
        }
    }

    function correctIndex(index) {
        return App.show.episodes.sourceIndex(index)
    }

    Connections {
        target: App.show
        function onShowChanged() { epFilterField.text = ""; libraryComboBox.rebuildModel() }
    }
    Connections {
        target: App.library
        function onLibraryChanged() { libraryComboBox.rebuildModel() }
    }

    Image {
        id: bgImage
        anchors.fill: parent
        source: infoPage.currentShow.coverUrl
        fillMode: Image.PreserveAspectCrop
        visible: false
    }
    FastBlur {
        anchors.fill: parent
        source: bgImage
        radius: 80
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(Theme.background, 0.82) }
            GradientStop { position: 0.4; color: Qt.alpha(Theme.background, 0.94) }
            GradientStop { position: 1.0; color: Theme.background }
        }
    }

    Card {
        id: episodePanel
        width: Math.min(parent.width * 0.32, 420)
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
            topMargin: 12
            rightMargin: 12
            bottomMargin: 12
        }
        radius: 16
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Layout.margins: 8
                Layout.bottomMargin: 0
                spacing: 6

                Text {
                    text: "EPISODES"
                    color: Theme.textMuted
                    Layout.fillWidth: true
                    font {
                        pixelSize: Globals.sp(20)
                        bold: true
                        letterSpacing: 1.5
                    }
                }

                Rectangle {
                    visible: episodeListView.count > 0
                    Layout.preferredWidth: epCountText.implicitWidth + 14
                    Layout.preferredHeight: 22
                    radius: 11
                    color: Theme.accent
                    Text {
                        id: epCountText
                        anchors.centerIn: parent
                        text: episodeListView.count
                        color: "white"
                        font {
                            pixelSize: Globals.sp(20)
                            bold: true
                        }
                    }
                }

                IconButton {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    iconName: "arrow-up-down"
                    iconSize: 18
                    boxRadius: 6
                    iconColor: Theme.textMuted
                    iconHoverColor: Theme.textAccent
                    onClicked: App.show.episodes.reversed = !App.show.episodes.reversed
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                color: Theme.border
            }

            AppTextField {
                id: epFilterField
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 2
                placeholderText: qsTr("Filter episodes...")
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                fontSize: 20
                onTextChanged: App.show.episodes.filterText = text
            }

            ListView {
                id: episodeListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 4
                clip: true
                model: App.show.episodes
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds

                property int lastWatchedIndex: App.show.episodes.visibleIndex(App.show.lastWatchedIndex)

                Component.onCompleted: {
                    if (lastWatchedIndex >= 0)
                        positionViewAtIndex(lastWatchedIndex, ListView.Center)
                }

                ScrollBar.vertical: AppScrollBar { width: 9; minimumSize: 0.06 }

                delegate: Rectangle {
                    id: ep
                    required property string title
                    required property real episodeNumber
                    required property int seasonNumber
                    required property int index
                    property bool isCurrent: episodeListView.lastWatchedIndex === index
                    property bool hovered: false

                    width: episodeListView.width
                    height: 52
                    radius: 10
                    color: isCurrent ? Qt.alpha(Theme.accent, 0.14) : (hovered ? Qt.alpha(Theme.accent, 0.08) : "transparent")
                    border.color: isCurrent ? Qt.alpha(Theme.accent, 0.35) : "transparent"
                    border.width: isCurrent ? 1 : 0
                    Behavior on color { ColorAnimation { duration: 80 } }

                    Rectangle {
                        visible: ep.isCurrent
                        width: 3
                        radius: 1.5
                        color: Theme.accent
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                            leftMargin: 1
                            topMargin: 8
                            bottomMargin: 8
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.AllButtons
                        onEntered: ep.hovered = true
                        onExited: ep.hovered = false
                        onClicked: (mouse) => {
                            const ci = infoPage.correctIndex(ep.index)
                            App.show.lastWatchedIndex = ci
                            App.playFromEpisodeList(ci, mouse.button === Qt.RightButton)
                        }
                    }

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 8
                            rightMargin: 6
                        }
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: Math.max(40, epNumText.implicitWidth + 14)
                            Layout.preferredHeight: 40
                            radius: 10
                            color: ep.isCurrent ? Theme.accent : Theme.surfaceDeep
                            border.color: ep.isCurrent ? Theme.accentLight : Theme.border
                            border.width: 1

                            Text {
                                id: epNumText
                                anchors.centerIn: parent
                                text: {
                                    const n = ep.episodeNumber
                                    return Math.floor(n) === n ? Math.floor(n).toString() : n.toFixed(1)
                                }
                                color: ep.isCurrent ? "white" : Theme.textMuted
                                font {
                                    pixelSize: Globals.sp(20)
                                    bold: true
                                }
                            }

                            PulseRing {
                                visible: ep.isCurrent
                                running: ep.isCurrent
                                anchors.fill: parent
                                radius: parent.radius
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Text {
                                text: {
                                    const season = ep.seasonNumber > 0
                                        ? "S" + ep.seasonNumber.toString().padStart(2, '0') + " " : ""
                                    const n = ep.episodeNumber
                                    return season + (Math.floor(n) === n
                                        ? "E" + Math.floor(n).toString().padStart(2, '0')
                                        : "E" + n.toFixed(1))
                                }
                                font {
                                    pixelSize: Globals.sp(20)
                                    bold: true
                                }
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: ep.isCurrent ? Theme.textAccent : Theme.textSecondary
                            }

                            MarqueeText {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: ep.title
                                fontSize: 20
                                color: ep.isCurrent ? Theme.accentLight : Theme.textMuted
                                spacing: 30
                                marqueeSpeed: 50
                            }
                        }

                        IconButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            visible: !ep.isCurrent
                            iconName: "tv"
                            iconSize: 18
                            boxRadius: 8
                            iconHoverColor: Theme.textSecondary
                            onClicked: {
                                const sourceIndex = infoPage.correctIndex(ep.index)
                                App.show.lastWatchedIndex = sourceIndex
                                App.library.updateProgress(infoPage.currentShow.link, sourceIndex, 0)
                            }
                        }

                        IconButton {
                            id: dlBtn
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            property bool downloaded: false
                            iconName: downloaded ? "check" : "download"
                            iconSize: 18
                            boxRadius: 8
                            active: downloaded
                            hoverColor: downloaded ? Qt.alpha(Theme.success, 0.15) : Theme.border
                            iconColor: downloaded ? Theme.success : Theme.textMuted
                            iconHoverColor: downloaded ? Theme.success : Theme.textMuted
                            onClicked: {
                                downloaded = true
                                enabled = false
                                App.downloadCurrentShow(infoPage.correctIndex(ep.index))
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: infoFlickable
        anchors {
            top: parent.top
            left: parent.left
            right: episodePanel.left
            bottom: parent.bottom
            topMargin: 12
            leftMargin: 12
            rightMargin: 10
            bottomMargin: 12
        }
        contentHeight: infoCol.implicitHeight + 24
        contentWidth: width
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: infoCol
            width: infoFlickable.width
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Rectangle {
                    Layout.preferredWidth: Math.min(infoFlickable.width * 0.28, 220)
                    Layout.preferredHeight: Layout.preferredWidth * 1.42
                    radius: 14
                    color: Theme.surfaceDeep
                    clip: true
                    border.color: Theme.border
                    border.width: 1

                    Image {
                        id: poster
                        anchors {
                            fill: parent
                            margins: 2
                        }
                        source: infoPage.currentShow.coverUrl
                        fillMode: Image.PreserveAspectCrop
                    }

                    // Separate placeholder: reassigning poster.source would kill its binding for good.
                    Image {
                        anchors {
                            fill: parent
                            margins: 2
                        }
                        fillMode: Image.PreserveAspectCrop
                        visible: poster.status === Image.Error
                        source: visible ? "qrc:/AoNami/resources/images/error_image.png" : ""
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: posterHover.containsMouse ? Qt.alpha(Theme.accent, 0.18) : "transparent"
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }

                    MouseArea {
                        id: posterHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: coverPopup.open()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: infoPage.currentShow.title
                        font {
                            pixelSize: Globals.sp(28)
                            bold: true
                        }
                        color: Theme.textPrimary
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.LeftButton) {
                                    Globals.lastSearch = infoPage.currentShow.title
                                    App.search(infoPage.currentShow.title)
                                    Globals.gotoPage(AppShell.Search)
                                } else {
                                    App.copyToClipboard(infoPage.currentShow.title)
                                }
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Rectangle {
                            visible: (infoPage.currentShow.status ?? "").length > 0
                            width: statusText.implicitWidth + 20
                            height: 30
                            radius: 15
                            color: {
                                let s = (infoPage.currentShow.status ?? "").toLowerCase()
                                if (s.includes("air") || s.includes("ongoing")) return Theme.success
                                if (s.includes("finish") || s.includes("complete")) return Theme.accent
                                return Theme.textMuted
                            }
                            Text {
                                id: statusText
                                anchors.centerIn: parent
                                text: infoPage.currentShow.status ?? ""
                                color: "white"
                                font {
                                    pixelSize: Globals.sp(20)
                                    bold: true
                                }
                            }
                        }

                        MetaChip {
                            iconName:   "star"
                            chipValue:  infoPage.currentShow.rating ?? ""
                            iconColor:  Theme.warning
                            valueColor: Theme.textPrimary
                        }

                        MetaChip {
                            iconName:  "eye"
                            chipValue: infoPage.currentShow.views ?? ""
                        }

                        Rectangle {
                            visible: infoPage.currentShow.provider?.name?.length > 0
                            width: provText.implicitWidth + 16
                            height: 30
                            radius: 15
                            color: Theme.surfaceAlt
                            border.color: Theme.border
                            border.width: 1
                            Text {
                                id: provText
                                anchors.centerIn: parent
                                text: infoPage.currentShow.provider?.name ?? ""
                                color: Theme.textAccent
                                font.pixelSize: Globals.sp(20)
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally(infoPage.currentShow.provider?.hostUrl ?? '#')
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        MetaChip {
                            pill: false
                            iconName: "calendar"
                            chipValue: infoPage.currentShow.releaseDate ?? ""
                            iconColor: Theme.textMuted
                            valueColor: Theme.textMuted
                        }
                        MetaChip {
                            pill: false
                            iconName: "history"
                            chipValue: infoPage.currentShow.updateTime ?? ""
                            iconColor: Theme.textMuted
                            valueColor: Theme.textMuted
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        AppButton {
                            visible: App.show.continueText.length > 0
                            text: ""
                            radius: height / 2
                            Layout.preferredHeight: 42
                            Layout.preferredWidth: Math.min(260, infoFlickable.width * 0.45)
                            leftPadding: 0
                            rightPadding: 0
                            backgroundDefaultColor: Theme.accent
                            contentItem: Item {
                                anchors {
                                    fill: parent
                                    leftMargin: 16
                                    rightMargin: 16
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8
                                    AppIcon {
                                        name: "play"
                                        size: 18
                                        color: "white"
                                    }
                                    MarqueeText {
                                        Layout.fillWidth: true
                                        color: "white"
                                        text: App.show.continueText
                                        fontSize: 20
                                        spacing: 30
                                        marqueeSpeed: 50
                                    }
                                }
                            }
                            onClicked: App.continueWatching()
                        }

                        AppComboBox {
                            id: libraryComboBox
                            Layout.preferredHeight: 42
                            Layout.preferredWidth: Math.min(180, infoFlickable.width * 0.28)
                            focus: false
                            activeFocusOnTab: false
                            placeholderText: "+ Library"
                            currentIndex: -1

                            function rebuildModel() {
                                libraryTypeModel.clear()
                                const types = App.library.typeNames
                                const lt = App.library.libraryTypeOf(infoPage.currentShow.link)
                                if (lt === -1) {
                                    for (let i = 0; i < types.length; i++)
                                        libraryTypeModel.append({ text: types[i], disabled: false })
                                    placeholderText = "+ Library"
                                    currentIndex = -1
                                } else {
                                    libraryTypeModel.append({ text: "Remove", disabled: false })
                                    for (let i = 0; i < types.length; i++)
                                        libraryTypeModel.append({ text: types[i], disabled: i === lt })
                                    placeholderText = ""
                                    currentIndex = lt + 1
                                }
                            }

                            Component.onCompleted: rebuildModel()

                            onActivated: (index) => {
                                const lt = App.library.libraryTypeOf(infoPage.currentShow.link)
                                if (lt === -1) {
                                    App.addToLibrary(-1, index)
                                } else if (index === 0) {
                                    App.library.remove(infoPage.currentShow.link)
                                } else {
                                    let t = index - 1
                                    if (t !== lt) App.addToLibrary(-1, t)
                                }
                                rebuildModel()
                            }

                            model: ListModel { id: libraryTypeModel }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Flow {
                visible: (infoPage.currentShow.genresString ?? "").length > 0
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: (infoPage.currentShow.genresString ?? "").split(",").map(s => s.trim()).filter(s => s.length > 0)
                    delegate: Rectangle {
                        id: genreChip
                        required property string modelData
                        width: chipText.implicitWidth + 20
                        height: 32
                        radius: 16
                        color: chipMa.containsMouse ? Qt.alpha(Theme.accent, 0.18) : Theme.surfaceAlt
                        border.color: chipMa.containsMouse ? Theme.accent : Theme.border
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Behavior on border.color { ColorAnimation { duration: 120 } }

                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: genreChip.modelData
                            color: chipMa.containsMouse ? Theme.textAccent : Theme.textMuted
                            font.pixelSize: Globals.sp(20)
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }

                        MouseArea {
                            id: chipMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Globals.lastSearch = genreChip.modelData
                                App.search(genreChip.modelData)
                                Globals.gotoPage(AppShell.Search)
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                implicitHeight: descCol.implicitHeight + 24
                radius: 14

                ColumnLayout {
                    id: descCol
                    anchors {
                        fill: parent
                        margins: 12
                    }
                    spacing: 8

                    Text {
                        text: "SYNOPSIS"
                        color: Theme.textMuted
                        font {
                            pixelSize: Globals.sp(20)
                            bold: true
                            letterSpacing: 1.5
                        }
                    }

                    RichText {
                        Layout.fillWidth: true
                        text: infoPage.currentShow.description.length > 0
                              ? infoPage.currentShow.description : "No Description"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    text: "LINKS"
                    color: Theme.textMuted
                    font {
                        pixelSize: Globals.sp(20)
                        bold: true
                    }
                }

                Repeater {
                    model: [
                        { icon: "https://anilist.co/img/icons/android-chrome-192x192.png",
                          url: "https://anilist.co/search/anime?search=" },
                        { icon: "https://myanimelist.net/img/common/pwa/launcher-icon-3x.png",
                          url: "https://myanimelist.net/search/all?q=" },
                        { icon: "https://m.media-amazon.com/images/G/01/imdb/images-ANDW73HA/favicon_desktop_32x32._CB1582158068_.png",
                          url: "https://www.imdb.com/find?q=" },
                        { icon: "https://cdn-icons-png.flaticon.com/512/3670/3670356.png",
                          url: "https://movie.douban.com/subject_search?search_text=" }
                    ]
                    delegate: Image {
                        id: linkButton
                        required property var modelData
                        source: linkButton.modelData.icon
                        fillMode: Image.PreserveAspectFit
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36

                        scale: linkArea.pressed ? 0.92 : (linkArea.containsMouse ? 1.06 : 1.0)
                        Behavior on scale { NumberAnimation { duration: 100; easing.type: Easing.OutCubic } }

                        MouseArea {
                            id: linkArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(
                                linkButton.modelData.url + encodeURIComponent(infoPage.currentShow.title))
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                visible: episodeListView.count > 0
                Layout.fillWidth: true
                spacing: 8

                AppIcon {
                    name: "download"
                    size: 18
                    color: Theme.textMuted
                }

                AppSpinBox {
                    id: startSpinBox
                    Layout.preferredWidth: 110
                    value: App.show.lastWatchedIndex + 1
                    from: 1
                    to: episodeListView.count
                    onValueModified: {
                        if (value > endSpinBox.value)
                            endSpinBox.value = value
                    }
                    onToChanged: value = App.show.lastWatchedIndex + 1
                }

                AppIcon {
                    name: "arrow-right"
                    size: 18
                    color: Theme.textMuted
                }

                AppSpinBox {
                    id: endSpinBox
                    Layout.preferredWidth: 110
                    value: episodeListView.count
                    from: 1
                    to: episodeListView.count
                    onValueModified: {
                        if (value < startSpinBox.value)
                            startSpinBox.value = value
                    }
                }

                AppButton {
                    text: "Download"
                    Layout.preferredHeight: 38
                    onClicked: App.downloadCurrentShow(startSpinBox.value - 1, endSpinBox.value - 1)
                }

                Item { Layout.fillWidth: true }
            }

            Item { Layout.preferredHeight: 12 }
        }
    }

    Popup {
        id: coverPopup
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        anchors.centerIn: parent
        height: Math.min(Globals.appHeight > 0 ? Globals.appHeight * 0.85 : 880, 880)
        width: height * 0.705
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: Rectangle { color: "transparent" }
        contentItem: Rectangle {
            color: "transparent"
            radius: 12
            clip: true
            Image {
                anchors.fill: parent
                source: infoPage.currentShow.coverUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            TapHandler { onTapped: coverPopup.close() }
        }
    }

    Keys.enabled: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space)
            App.continueWatching()
        else if (event.key === Qt.Key_Escape)
            infoPage.forceActiveFocus()
    }

    Component.onCompleted: infoPage.forceActiveFocus()
}