import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia

ApplicationWindow {
    id: window
    visible: false
    // Compact default geometry while preserving the original aspect ratio.
    width: 1104
    height: 672
    minimumWidth: 960
    minimumHeight: 640
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.Tool | (pinned ? Qt.WindowStaysOnTopHint : 0)
    title: "PurrFind"

    property string propertiesText: ""
    property bool pinned: true
    property bool advancedFiltersExpanded: false
    property bool previewPanelVisible: true
    property bool previewTextExpanded: false
    property string themeMode: "system"
    readonly property string appVersion: Qt.application.version || "desenvolvimento"
    readonly property bool darkTheme: themeMode === "dark"
        || (themeMode === "system" && Qt.styleHints.colorScheme !== Qt.Light)
    readonly property var theme: darkTheme ? ({
        window: "#0b1422", surface: "#101c2c", header: "#16263b", panel: "#101d2d",
        elevated: "#17283b", input: "#43356f", text: "#f2efff", muted: "#aeb8d0",
        subtle: "#65738b", border: "#35445d", chip: "#202d41", chipBorder: "#2d3c53",
        preview: "#09131f", accent: "#9667ff", accentSoft: "#35245f"
    }) : ({
        window: "#f5f7fb", surface: "#ffffff", header: "#eef3fa", panel: "#ffffff",
        elevated: "#f0f4fa", input: "#e8e2ff", text: "#14213b", muted: "#52627b",
        subtle: "#687892", border: "#c8d2e2", chip: "#e9eef6", chipBorder: "#c8d4e5",
        preview: "#f7f9fc", accent: "#5a35c9", accentSoft: "#e2d9ff"
    })
    readonly property color accent: theme.accent
    readonly property color accentSoft: theme.accentSoft
    readonly property color panel: theme.panel
    readonly property color border: theme.border
    readonly property color primaryText: theme.text
    readonly property color secondaryText: theme.muted
    readonly property bool previewIsVideo: results.currentItem !== null
                                            && results.currentItem.mimeType.startsWith("video/")

    function categoryFilter(index) {
        return ["", "kind:file", "kind:folder", "category:image", "category:document",
                "category:video", "category:other"][index]
    }
    function runSearch() { purrfindController.search(searchField.text, categoryFilter(categoryTabs.currentIndex)) }
    function appendFilter(token) {
        let value = searchField.text.trim()
        let separator = token.indexOf(":")
        if (separator > 0) {
            let key = token.substring(0, separator)
            let previous = new RegExp("(^|\\s)" + key + ":(?:\"[^\"]*\"|\\S+)", "gi")
            value = value.replace(previous, " ").replace(/\s+/g, " ").trim()
        }
        searchField.text = value.length ? value + " " + token : token
        searchField.forceActiveFocus()
    }
    function escaped(value) { return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function highlighted(value, term) {
        let plain = term.trim().split(/\s+/).filter(x => x.indexOf(":") < 0).join(" ")
        let safe = escaped(value)
        if (!plain.length) return safe
        let pattern = plain.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
        let highlightColor = window.darkTheme ? "#cdb7ff" : "#5031a8"
        return safe.replace(new RegExp("(" + pattern + ")", "ig"), "<font color='" + highlightColor + "'><b>$1</b></font>")
    }
    function snippetHtml(value) {
        let highlightColor = window.darkTheme ? "#cdb7ff" : "#5031a8"
        return escaped(value).replace(/\[\[PFH\]\]/g, "<font color='" + highlightColor + "'><b>")
                             .replace(/\[\[\/PFH\]\]/g, "</b></font>")
    }
    function previewHtml(value) {
        let html = value.indexOf("[[PFH]]") >= 0 ? snippetHtml(value) : highlighted(value, searchField.text)
        html = html.replace(/\[\[PFFOLDER\]\]/g, "<font color='#c187ff'><b>■</b></font>&nbsp;")
                   .replace(/\[\[PFIMAGE\]\]/g, "<font color='#49d7c8'><b>▣</b></font>&nbsp;")
                   .replace(/\[\[PFVIDEO\]\]/g, "<font color='#ff7997'><b>▶</b></font>&nbsp;")
                   .replace(/\[\[PFDOC\]\]/g, "<font color='#62a4ff'><b>▤</b></font>&nbsp;")
                   .replace(/\[\[PFARCHIVE\]\]/g, "<font color='#e7b85c'><b>◆</b></font>&nbsp;")
                   .replace(/\[\[PFFILE\]\]/g, "<font color='#9aa8bd'><b>•</b></font>&nbsp;")
        return html.replace(/\r\n|\r|\n/g, "<br>")
    }
    function mediaTime(milliseconds) {
        let seconds = Math.max(0, Math.floor(milliseconds / 1000))
        let minutes = Math.floor(seconds / 60)
        let remaining = seconds % 60
        return minutes + ":" + (remaining < 10 ? "0" : "") + remaining
    }

    AudioOutput { id: previewAudio; volume: 0.75 }
    MediaPlayer {
        id: videoPlayer
        source: window.previewIsVideo && results.currentItem ? results.currentItem.fileUrl : ""
        videoOutput: videoSurface
        audioOutput: previewAudio
    }

    Shortcut { sequence: "Escape"; onActivated: settings.visible ? settings.close() : window.hide() }
    Shortcut { sequence: "Ctrl+Return"; onActivated: purrfindController.reveal(results.currentIndex) }
    Shortcut { sequence: "Ctrl+P"; onActivated: window.previewPanelVisible = !window.previewPanelVisible }
    Shortcut { sequence: "Ctrl+Shift+C"; onActivated: purrfindController.copyPath(results.currentIndex) }
    Shortcut {
        sequence: "Alt+Return"
        onActivated: if (results.currentIndex >= 0) {
            window.propertiesText = purrfindController.properties(results.currentIndex)
                    + (purrfindController.previewDetails.length ? "\n\n" + purrfindController.previewDetails : "")
            propertiesDialog.open()
        }
    }

    component HeaderGlyph: Item {
        property string kind: ""
        property bool active: false
        property color strokeColor: active ? window.accent : window.secondaryText
        implicitWidth: 20; implicitHeight: 20
        onStrokeColorChanged: glyphCanvas.requestPaint()
        onKindChanged: glyphCanvas.requestPaint()
        onActiveChanged: glyphCanvas.requestPaint()
        Canvas {
            id: glyphCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.save()
                ctx.scale(width / 22, height / 22)
                ctx.strokeStyle = parent.strokeColor
                ctx.fillStyle = parent.strokeColor
                ctx.lineWidth = 1.7
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                if (parent.kind === "filter") {
                    ctx.beginPath()
                    ctx.moveTo(3, 4); ctx.lineTo(19, 4); ctx.lineTo(13, 11)
                    ctx.lineTo(13, 18); ctx.lineTo(9, 16); ctx.lineTo(9, 11); ctx.closePath()
                    ctx.stroke()
                } else if (parent.kind === "preview") {
                    ctx.beginPath()
                    ctx.moveTo(2.5, 11)
                    ctx.quadraticCurveTo(11, 2.5, 19.5, 11)
                    ctx.quadraticCurveTo(11, 19.5, 2.5, 11)
                    ctx.stroke()
                    ctx.beginPath(); ctx.arc(11, 11, 2.5, 0, Math.PI * 2); ctx.stroke()
                    if (!parent.active) {
                        ctx.beginPath(); ctx.moveTo(4, 4); ctx.lineTo(18, 18); ctx.stroke()
                    }
                } else if (parent.kind === "settings") {
                    var teeth = 8
                    ctx.beginPath()
                    for (var t = 0; t < teeth * 4; ++t) {
                        var toothAngle = -Math.PI / 2 + t * Math.PI / (teeth * 2)
                        var toothRadius = (t % 4 === 1 || t % 4 === 2) ? 9.5 : 7.2
                        var toothX = 11 + Math.cos(toothAngle) * toothRadius
                        var toothY = 11 + Math.sin(toothAngle) * toothRadius
                        if (t === 0) ctx.moveTo(toothX, toothY)
                        else ctx.lineTo(toothX, toothY)
                    }
                    ctx.closePath(); ctx.fill()
                    ctx.globalCompositeOperation = "destination-out"
                    ctx.beginPath(); ctx.arc(11, 11, 3.2, 0, Math.PI * 2); ctx.fill()
                    ctx.globalCompositeOperation = "source-over"
                } else if (parent.kind === "pin") {
                    ctx.beginPath()
                    ctx.moveTo(13.5, 2.5); ctx.lineTo(19.5, 8.5); ctx.lineTo(16, 12)
                    ctx.lineTo(12, 8); ctx.lineTo(6, 14); ctx.lineTo(3.5, 20.5)
                    ctx.lineTo(10, 18); ctx.lineTo(16, 12); ctx.stroke()
                }
                ctx.restore()
            }
        }
    }
    component HeaderIconButton: ToolButton {
        implicitWidth: 48; implicitHeight: 48; font.pixelSize: 23
        contentItem: Text {
            text: parent.text; color: parent.hovered ? window.primaryText : window.secondaryText; font: parent.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            anchors.fill: parent
            radius: 10
            color: parent.highlighted ? window.theme.accentSoft
                  : (parent.hovered ? window.theme.elevated : window.theme.chip)
            border.color: parent.highlighted ? window.accent
                          : (parent.hovered ? window.theme.border : window.theme.chipBorder)
            border.width: 1
        }
    }
    component FilterChip: Button {
        implicitHeight: 34; leftPadding: 13; rightPadding: 13; font.pixelSize: 12
        contentItem: Text {
            text: parent.text; color: parent.hovered ? window.primaryText : window.secondaryText; font: parent.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 8; color: parent.hovered ? window.theme.elevated : window.theme.chip
            border.color: parent.hovered ? window.theme.border : window.theme.chipBorder
        }
    }
    component FilterCombo: ComboBox {
        implicitHeight: 36
        leftPadding: 12
        rightPadding: 34
        font.pixelSize: 11
        contentItem: Text {
            leftPadding: 0
            text: parent.displayText
            color: window.primaryText
            font: parent.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: parent.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            text: "▾"
            color: window.secondaryText
            font.pixelSize: 12
        }
        background: Rectangle {
            radius: 7
            color: parent.hovered || parent.activeFocus ? window.theme.elevated : window.theme.chip
            border.color: parent.activeFocus ? window.accent : window.theme.border
        }
    }
    component KeyCap: Rectangle {
        required property string label
        implicitWidth: keyLabel.implicitWidth + 14; implicitHeight: 25; radius: 6
        color: window.theme.chip; border.color: window.theme.border
        Text {
            id: keyLabel; anchors.centerIn: parent; text: parent.label
            color: window.primaryText; font.pixelSize: 10; font.bold: true
        }
    }

    Rectangle {
        anchors.fill: parent; radius: 20; color: theme.window
        border.color: theme.border; border.width: 1; clip: true
        Rectangle {
            anchors.fill: parent; radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0; color: theme.header }
                GradientStop { position: 0.55; color: theme.surface }
                GradientStop { position: 1; color: theme.window }
            }
        }

        ColumnLayout {
            anchors.fill: parent; spacing: 0

            Rectangle {
                id: header
                Layout.fillWidth: true; Layout.preferredHeight: 92
                color: theme.header; border.color: window.border
                DragHandler { target: null; onActiveChanged: if (active) window.startSystemMove() }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 20; spacing: 16
                    RowLayout {
                        Layout.preferredWidth: 190; spacing: 11
                        Image {
                            Layout.preferredWidth: 190; Layout.preferredHeight: 64
                            source: window.darkTheme
                                ? "qrc:/resources/icons/logo_fundo_escuro.png"
                                : "qrc:/resources/icons/logo_fundo_claro.png"
                            sourceSize: Qt.size(380, 127); fillMode: Image.PreserveAspectFit
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.maximumWidth: 710; Layout.preferredHeight: 58
                        radius: 12; color: searchField.activeFocus ? window.accentSoft : window.theme.input
                        border.color: searchField.activeFocus ? window.accent : window.theme.border
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 17; anchors.rightMargin: 10; spacing: 12
                            Text { text: "⌕"; color: window.accentSoft; font.pixelSize: 31 }
                            TextField {
                                id: searchField
                                Layout.fillWidth: true
                                placeholderText: "Pesquise arquivos, conteúdos e metadados..."
                                placeholderTextColor: window.secondaryText; color: window.primaryText
                                font.pixelSize: 19; font.weight: Font.Medium
                                leftPadding: 0; rightPadding: 0; background: Item {}
                                focus: true; selectByMouse: true
                                Accessible.name: "Pesquisar arquivos, conteúdos e metadados"
                                onTextChanged: searchDebounce.restart()
                                Keys.onDownPressed: {
                                    results.currentIndex = Math.min(results.count - 1, results.currentIndex + 1)
                                    results.forceActiveFocus()
                                }
                                Keys.onUpPressed: results.currentIndex = Math.max(0, results.currentIndex - 1)
                                Keys.onReturnPressed: {
                                    if (event.modifiers & Qt.ControlModifier) purrfindController.reveal(results.currentIndex)
                                    else purrfindController.open(results.currentIndex)
                                }
                            }
                            HeaderIconButton {
                                visible: searchField.text.length > 0; text: "×"; font.pixelSize: 29
                                Accessible.name: "Limpar pesquisa"
                                onClicked: { searchField.clear(); searchField.forceActiveFocus() }
                            }
                        }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 52; color: theme.border }
                    HeaderIconButton {
                        contentItem: Item {
                            HeaderGlyph { width: 20; height: 20; anchors.centerIn: parent; kind: "filter" }
                        }
                        Accessible.name: "Abrir filtros"
                        ToolTip.visible: hovered; ToolTip.text: "Filtros"
                        onClicked: filtersPopup.open()
                    }
                    HeaderIconButton {
                        contentItem: Item {
                            HeaderGlyph { width: 20; height: 20; anchors.centerIn: parent; kind: "preview"; active: window.previewPanelVisible }
                        }
                        highlighted: window.previewPanelVisible
                        Accessible.name: window.previewPanelVisible ? "Ocultar pré-visualização" : "Mostrar pré-visualização"
                        ToolTip.visible: hovered; ToolTip.text: "Pré-visualização (Ctrl+P)"
                        onClicked: window.previewPanelVisible = !window.previewPanelVisible
                    }
                    HeaderIconButton {
                        contentItem: Item {
                            HeaderGlyph { width: 20; height: 20; anchors.centerIn: parent; kind: "settings" }
                        }
                        Accessible.name: "Abrir configurações"
                        ToolTip.visible: hovered; ToolTip.text: "Configurações"
                        onClicked: settings.open()
                    }
                    HeaderIconButton {
                        contentItem: Item {
                            HeaderGlyph { width: 20; height: 20; anchors.centerIn: parent; kind: "pin"; active: window.pinned }
                        }
                        highlighted: window.pinned
                        checkable: true; checked: window.pinned
                        Accessible.name: window.pinned ? "Desafixar janela" : "Fixar janela"
                        ToolTip.visible: hovered
                        ToolTip.text: window.pinned ? "Janela sempre visível" : "Manter janela sempre visível"
                        onToggled: window.pinned = checked
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                TabBar {
                        id: categoryTabs
                        Layout.fillWidth: true; Layout.preferredHeight: 56
                        leftPadding: 12; rightPadding: 12; topPadding: 7; bottomPadding: 7; spacing: 12
                        background: Rectangle { color: theme.surface; border.color: window.border }
                        Repeater {
                            model: ["Todos", "Arquivos", "Pastas", "Imagens",
                                    "Documentos", "Vídeos", "Outros"]
                            TabButton {
                                id: categoryTab
                                required property int index
                                required property string modelData
                                // Base the width on the window, not TabBar.implicitWidth,
                                // avoiding a binding loop in the KDE Qt style.
                                width: Math.max(96, (window.width - categoryTabs.leftPadding
                                            - categoryTabs.rightPadding - categoryTabs.spacing * 6) / 7)
                                text: modelData + " (" + purrfindController.categoryCounts[index] + ")"
                                implicitHeight: 40
                                font.pixelSize: 11; leftPadding: 8; rightPadding: 8
                                leftInset: 4; rightInset: 4; topInset: 1; bottomInset: 1
                                contentItem: Item {
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 7
                                        Image {
                                            width: 18; height: 18
                                            source: "qrc:/resources/icons/categories/" +
                                                    ["all", "file", "folder", "image", "document", "video", "other"][index] + ".svg"
                                            sourceSize: Qt.size(36, 36); fillMode: Image.PreserveAspectFit
                                            verticalAlignment: Image.AlignVCenter
                                        }
                                        Text {
                                            text: categoryTab.text
                                            color: categoryTab.checked ? "#ffffff" : window.primaryText
                                            font: categoryTab.font
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                                background: Rectangle {
                                    // Keep a visible gutter even on Qt styles that
                                    // collapse Container spacing around repeater items.
                                    radius: 8
                                    color: parent.checked
                                           ? window.accent
                                           : (parent.hovered ? window.theme.elevated : window.theme.chip)
                                    border.color: parent.checked ? window.accent : window.theme.chipBorder
                                }
                            }
                        }
                        onCurrentIndexChanged: searchDebounce.restart()
                }

                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                        Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; color: theme.window
                        ListView {
                            id: results
                            anchors.fill: parent
                            anchors.leftMargin: 20; anchors.rightMargin: 16; anchors.topMargin: 12; anchors.bottomMargin: 10
                            clip: true; spacing: 2; model: purrfindController.results
                            reuseItems: true; cacheBuffer: 320
                            boundsBehavior: Flickable.StopAtBounds
                            currentIndex: count ? 0 : -1; keyNavigationEnabled: true
                            Accessible.name: "Resultados da pesquisa"
                            highlightMoveDuration: 70
                            onCurrentIndexChanged: {
                                videoPlayer.stop()
                                window.previewTextExpanded = false
                                purrfindController.select(currentIndex)
                            }
                            Keys.onReturnPressed: {
                                if (event.modifiers & Qt.ControlModifier) purrfindController.reveal(currentIndex)
                                else purrfindController.open(currentIndex)
                            }
                            Keys.onEscapePressed: window.hide()
                            ScrollBar.vertical: ScrollBar {}
                            highlight: Rectangle { radius: 10; color: window.accentSoft; border.color: window.accent }

                            delegate: ItemDelegate {
                                id: resultDelegate
                                required property int index
                                required property string name
                                required property string path
                                required property string parentPath
                                required property string extension
                                required property string mimeType
                                required property string sizeText
                                required property string modifiedText
                                required property url fileUrl
                                required property bool directory
                                required property string snippet
                                required property string matchOrigin
                                required property string documentTitle
                                required property string documentAuthor
                                required property int pageCount
                                width: results.width; height: 78; hoverEnabled: true; clip: true
                                leftPadding: 8; rightPadding: 8; topPadding: 5; bottomPadding: 5
                                background: Rectangle {
                                    radius: 10; color: resultDelegate.hovered ? window.theme.elevated : "transparent"
                                    border.color: resultDelegate.hovered ? window.theme.border : "transparent"
                                    Rectangle {
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                        anchors.leftMargin: 64; height: 1; color: window.theme.border
                                        visible: !resultDelegate.ListView.isCurrentItem
                                    }
                                }
                                onClicked: { results.currentIndex = index; results.forceActiveFocus() }
                                onDoubleClicked: purrfindController.open(index)
                                Accessible.name: name + ", " + parentPath
                                contentItem: RowLayout {
                                    spacing: 13; clip: true
                                    Rectangle {
                                        Layout.preferredWidth: 48; Layout.preferredHeight: 48; radius: 9
                                        Layout.minimumWidth: 48; Layout.maximumWidth: 48
                                        color: directory ? (window.darkTheme ? "#3f315f" : "#e7defb")
                                                         : mimeType.startsWith("image/") ? (window.darkTheme ? "#204a55" : "#d8f0f2")
                                                         : window.theme.chip
                                        Image {
                                            anchors.centerIn: parent; width: 34; height: 34
                                            source: purrfindController.iconUrl(mimeType, directory)
                                            sourceSize: Qt.size(68, 68); fillMode: Image.PreserveAspectFit
                                        }
                                    }
                                    ColumnLayout {
                                        id: resultTextColumn
                                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 1; clip: true
                                        RowLayout {
                                            Layout.fillWidth: true; spacing: 7; clip: true
                                            Text {
                                                Layout.fillWidth: true; Layout.minimumWidth: 0
                                                text: window.highlighted(name, searchField.text)
                                                textFormat: Text.StyledText; color: window.primaryText
                                                font.pixelSize: 14; font.weight: Font.DemiBold
                                                elide: Text.ElideRight; maximumLineCount: 1; wrapMode: Text.NoWrap; clip: true
                                            }
                                            Rectangle {
                                                visible: matchOrigin === "ocr"
                                                Layout.preferredWidth: 35; Layout.minimumWidth: 35; Layout.preferredHeight: 19
                                                radius: 5; color: window.darkTheme ? "#623352" : "#ead8f2"
                                                Text { anchors.centerIn: parent; text: "OCR"; color: window.darkTheme ? "#ffd0ea" : "#713d7c"; font.pixelSize: 8; font.bold: true }
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0; visible: snippet.length > 0
                                            text: window.snippetHtml(snippet); textFormat: Text.StyledText
                                            color: window.secondaryText; font.pixelSize: 10
                                            elide: Text.ElideRight; maximumLineCount: 1; wrapMode: Text.NoWrap; clip: true
                                        }
                                        Text {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0; text: parentPath
                                            color: window.theme.subtle; font.pixelSize: 10
                                            elide: Text.ElideMiddle; maximumLineCount: 1; wrapMode: Text.NoWrap; clip: true
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.preferredWidth: 142; Layout.minimumWidth: 142; Layout.maximumWidth: 142; spacing: 2
                                        Text { Layout.fillWidth: true; text: sizeText; color: window.primaryText; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft }
                                        Text { Layout.fillWidth: true; text: extension; color: window.secondaryText; font.pixelSize: 10; font.bold: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft }
                                        Text { Layout.fillWidth: true; text: modifiedText; color: window.theme.subtle; font.pixelSize: 9; horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft }
                                    }
                                }
                            }

                            Column {
                                anchors.centerIn: parent; spacing: 8
                                visible: results.count === 0 && !purrfindController.searching
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: searchField.text.length ? "Nenhum resultado encontrado" : "Digite para começar a pesquisar"
                                    color: window.secondaryText; font.pixelSize: 15
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Pesquise por nome, conteúdo, OCR ou metadados"
                                    color: window.theme.subtle; font.pixelSize: 11
                                }
                            }
                            BusyIndicator { anchors.centerIn: parent; running: purrfindController.searching; visible: running }
                        }
                        }
                    }

                Rectangle {
                    id: sidePanel
                    // Keep preview available at the compact default window size.
                    visible: window.width >= 960 && window.previewPanelVisible
                    Layout.preferredWidth: 360; Layout.minimumWidth: 320; Layout.fillHeight: true
                    color: theme.surface; border.color: window.border; clip: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        RowLayout {
                            Layout.fillWidth: true; Layout.preferredHeight: 28; Layout.minimumHeight: 28
                            Text {
                                Layout.fillWidth: true
                                text: "PRÉ-VISUALIZAÇÃO"; color: window.accent
                                font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.4
                                elide: Text.ElideRight
                            }
                            Item {
                                Layout.preferredWidth: 92; Layout.minimumWidth: 92; Layout.maximumWidth: 92
                                Layout.preferredHeight: 28
                                Accessible.name: "Ocultar pré-visualização"
                                Accessible.role: Accessible.Button
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 7
                                    color: hidePreviewMouse.pressed ? window.theme.accentSoft
                                          : (hidePreviewMouse.containsMouse ? window.theme.elevated : "transparent")
                                    border.color: window.accent
                                    border.width: 1
                                }
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Canvas {
                                        width: 17; height: 17
                                        anchors.verticalCenter: parent.verticalCenter
                                        onPaint: {
                                            var ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            ctx.strokeStyle = window.accent
                                            ctx.lineWidth = 1.7
                                            ctx.lineCap = "round"
                                            ctx.beginPath()
                                            ctx.moveTo(1.5, 8.5)
                                            ctx.quadraticCurveTo(8.5, 1.2, 15.5, 8.5)
                                            ctx.quadraticCurveTo(8.5, 15.8, 1.5, 8.5)
                                            ctx.stroke()
                                            ctx.beginPath()
                                            ctx.arc(8.5, 8.5, 2.2, 0, Math.PI * 2)
                                            ctx.stroke()
                                            ctx.beginPath()
                                            ctx.moveTo(2.5, 2.5)
                                            ctx.lineTo(14.5, 14.5)
                                            ctx.stroke()
                                        }
                                    }
                                    Text {
                                        text: "Ocultar"; color: window.accent
                                        font.pixelSize: 11; font.bold: true
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                                MouseArea {
                                    id: hidePreviewMouse
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: window.previewPanelVisible = false
                                }
                            }
                        }
                        Rectangle {
                            id: previewCanvas
                            Layout.fillWidth: true; Layout.fillHeight: false
                            // Reserve room for page navigation and actions below;
                            // the preview scales within the remaining height.
                            Layout.minimumHeight: 150
                            // Keep a generous reserve for page navigation, actions
                            // and the footer so those controls are never clipped.
                            Layout.preferredHeight: Math.max(130, sidePanel.height - 150)
                            radius: 12; color: theme.preview; border.color: window.border; clip: true
                            Image {
                                anchors.fill: parent; anchors.margins: 10
                                visible: purrfindController.previewImageUrl.length > 0
                                source: purrfindController.previewImageUrl
                                fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                            }
                            Image {
                                anchors.centerIn: parent; width: 120; height: 120
                                visible: results.currentItem !== null
                                         && purrfindController.previewImageUrl.length === 0
                                         && purrfindController.previewText.length === 0
                                         && !window.previewIsVideo
                                         && !purrfindController.previewLoading
                                source: results.currentItem ? purrfindController.iconUrl(results.currentItem.mimeType, results.currentItem.directory) : ""
                                sourceSize: Qt.size(240, 240); fillMode: Image.PreserveAspectFit
                            }
                            ScrollView {
                                id: documentPreviewScroll
                                anchors.fill: parent; anchors.margins: 14
                                visible: purrfindController.previewImageUrl.length === 0
                                         && purrfindController.previewText.length > 0
                                         && !window.previewIsVideo
                                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                                TextEdit {
                                    width: documentPreviewScroll.availableWidth
                                    text: window.previewHtml(purrfindController.previewText)
                                    textFormat: Text.RichText; wrapMode: Text.Wrap
                                    color: window.primaryText; font.pixelSize: 12
                                    readOnly: true; selectByMouse: true
                                }
                            }
                            VideoOutput {
                                id: videoSurface
                                anchors.fill: parent; anchors.margins: 6; z: 2
                                visible: window.previewIsVideo
                                fillMode: VideoOutput.PreserveAspectFit
                            }
                            ToolButton {
                                anchors.centerIn: parent; width: 88; height: 88; z: 3
                                visible: window.previewIsVideo
                                         && videoPlayer.playbackState !== MediaPlayer.PlayingState
                                         && videoPlayer.error === MediaPlayer.NoError
                                Accessible.name: "Reproduzir vídeo"
                                contentItem: Text {
                                    text: "▶"; color: "white"; font.pixelSize: 35
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 44; color: parent.hovered ? window.accent : window.accentSoft
                                    border.color: window.accent; border.width: 1
                                }
                                onClicked: videoPlayer.play()
                            }
                            Rectangle {
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: 10; height: 48; radius: 9; z: 4
                                visible: window.previewIsVideo; color: window.theme.elevated; border.color: window.border
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 8
                                    ToolButton {
                                        Layout.preferredWidth: 34; Layout.preferredHeight: 34
                                        text: videoPlayer.playbackState === MediaPlayer.PlayingState ? "Ⅱ" : "▶"
                                        Accessible.name: videoPlayer.playbackState === MediaPlayer.PlayingState ? "Pausar" : "Reproduzir"
                                        onClicked: videoPlayer.playbackState === MediaPlayer.PlayingState
                                                   ? videoPlayer.pause() : videoPlayer.play()
                                    }
                                    Text { text: window.mediaTime(videoPlayer.position); color: window.primaryText; font.pixelSize: 9 }
                                    Slider {
                                        Layout.fillWidth: true; from: 0; to: Math.max(1, videoPlayer.duration)
                                        value: videoPlayer.position; Accessible.name: "Posição do vídeo"
                                        onMoved: videoPlayer.position = value
                                    }
                                    Text { text: window.mediaTime(videoPlayer.duration); color: window.primaryText; font.pixelSize: 9 }
                                    ToolButton {
                                        Layout.preferredWidth: 34; Layout.preferredHeight: 34
                                        text: previewAudio.muted ? "×♪" : "♪"
                                        Accessible.name: previewAudio.muted ? "Ativar áudio" : "Silenciar"
                                        onClicked: previewAudio.muted = !previewAudio.muted
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent; anchors.margins: 24; z: 5
                                visible: window.previewIsVideo && videoPlayer.error !== MediaPlayer.NoError
                                width: parent.width - 48; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                                text: "Não foi possível reproduzir este vídeo.\n" + videoPlayer.errorString
                                color: "#ff91a3"; font.pixelSize: 11
                            }
                            BusyIndicator {
                                anchors.centerIn: parent; z: 5
                                running: window.previewIsVideo
                                         && (videoPlayer.mediaStatus === MediaPlayer.LoadingMedia
                                             || videoPlayer.mediaStatus === MediaPlayer.BufferingMedia)
                                visible: running
                            }
                            Column {
                                anchors.centerIn: parent; spacing: 8
                                visible: results.currentItem === null && !purrfindController.previewLoading
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Selecione um resultado"; color: window.primaryText; font.pixelSize: 14; font.bold: true }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "PDFs, imagens, vídeos e documentos aparecem aqui"; color: window.theme.subtle; font.pixelSize: 10 }
                            }
                            BusyIndicator { anchors.centerIn: parent; running: purrfindController.previewLoading; visible: running }
                        }
                        RowLayout {
                            id: previewActions
                            Layout.fillWidth: true; Layout.preferredHeight: 44; Layout.minimumHeight: 44
                            visible: results.currentItem !== null
                            spacing: 8
                            ToolButton {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                enabled: purrfindController.previewPage > 1
                                text: "←"; font.pixelSize: 22
                                Accessible.name: "Página anterior"; ToolTip.visible: hovered; ToolTip.text: "Página anterior"
                                contentItem: Text {
                                    text: parent.text; color: window.secondaryText; opacity: 1
                                    font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 10; color: parent.hovered ? window.theme.elevated : window.theme.preview
                                    border.color: window.border; opacity: parent.enabled ? 1 : 0.45
                                }
                                onClicked: purrfindController.navigatePreview(-1)
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: purrfindController.previewPageCount > 0
                                text: purrfindController.previewPage + " / " + purrfindController.previewPageCount
                                color: window.accent; font.pixelSize: 12; font.bold: true
                                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            }
                            Item { Layout.fillWidth: true }
                            ToolButton {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                enabled: purrfindController.previewPage < purrfindController.previewPageCount
                                text: "→"; font.pixelSize: 22
                                Accessible.name: "Próxima página"; ToolTip.visible: hovered; ToolTip.text: "Próxima página"
                                contentItem: Text {
                                    text: parent.text; color: window.secondaryText; opacity: 1
                                    font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 10; color: parent.hovered ? window.theme.elevated : window.theme.preview
                                    border.color: window.border; opacity: parent.enabled ? 1 : 0.45
                                }
                                onClicked: purrfindController.navigatePreview(1)
                            }
                            Item {
                                Layout.fillWidth: true
                                visible: purrfindController.previewImageUrl.length > 0 && purrfindController.previewText.length > 0
                            }
                            ToolButton {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                visible: purrfindController.previewImageUrl.length > 0 && purrfindController.previewText.length > 0
                                text: "≡"; font.pixelSize: 21
                                Accessible.name: window.previewTextExpanded ? "Ocultar texto extraído" : "Ver texto extraído"
                                ToolTip.visible: hovered; ToolTip.text: window.previewTextExpanded ? "Ocultar texto extraído" : "Ver texto extraído"
                                contentItem: Text {
                                    text: parent.text; color: window.primaryText
                                    font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 10; color: parent.hovered ? window.theme.elevated : window.theme.preview
                                    border.color: window.border
                                }
                                onClicked: window.previewTextExpanded = !window.previewTextExpanded
                            }
                            ToolButton {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                text: "ⓘ"; font.pixelSize: 20
                                Accessible.name: "Detalhes"; ToolTip.visible: hovered; ToolTip.text: "Detalhes"
                                contentItem: Text {
                                    text: parent.text; color: window.primaryText
                                    font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 10; color: parent.hovered ? window.theme.elevated : window.theme.preview
                                    border.color: window.border
                                }
                                onClicked: {
                                    window.propertiesText = purrfindController.properties(results.currentIndex)
                                            + (purrfindController.previewDetails.length ? "\n\n" + purrfindController.previewDetails : "")
                                    propertiesDialog.open()
                                }
                            }
                            ToolButton {
                                Layout.preferredWidth: 42; Layout.preferredHeight: 42
                                text: "↗"; font.pixelSize: 22
                                Accessible.name: "Abrir arquivo"; ToolTip.visible: hovered; ToolTip.text: "Abrir arquivo"
                                contentItem: Text {
                                    text: parent.text; color: window.accent
                                    font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    radius: 10; color: parent.hovered ? window.theme.elevated : window.theme.preview
                                    border.color: window.border
                                }
                                onClicked: purrfindController.open(results.currentIndex)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: window.previewTextExpanded ? 115 : 0
                            visible: window.previewTextExpanded && purrfindController.previewText.length > 0
                            radius: 8; color: theme.preview; border.color: window.border; clip: true
                            ScrollView {
                                id: extractedTextScroll
                                anchors.fill: parent; anchors.margins: 9
                                TextEdit {
                                    width: extractedTextScroll.availableWidth; text: window.previewHtml(purrfindController.previewText)
                                    textFormat: Text.RichText; wrapMode: Text.Wrap
                                    color: window.primaryText; font.pixelSize: 10; readOnly: true; selectByMouse: true
                                }
                            }
                        }
                    }
                }
            }

            }

            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 47
                color: theme.header; border.color: window.border
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20; spacing: 8
                    KeyCap { label: "↑ ↓" }
                    Text { text: "Navegar"; color: window.secondaryText; font.pixelSize: 10 }
                    KeyCap { label: "Enter" }
                    Text { text: "Abrir"; color: window.secondaryText; font.pixelSize: 10 }
                    KeyCap { label: "Ctrl + Enter" }
                    Text { text: "Abrir pasta"; color: window.secondaryText; font.pixelSize: 10 }
                    KeyCap { label: "Ctrl + Shift + C" }
                    Text { text: "Copiar caminho"; color: window.secondaryText; font.pixelSize: 10 }
                    KeyCap { label: "Alt + Enter" }
                    Text { text: "Propriedades"; color: window.secondaryText; font.pixelSize: 10 }
                    KeyCap { label: "Esc" }
                    Text { text: "Fechar"; color: window.secondaryText; font.pixelSize: 10 }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "v" + window.appVersion
                        color: window.theme.subtle; font.pixelSize: 10
                        Accessible.name: "Versão " + window.appVersion
                    }
                    Text {
                        text: results.count + " resultados em " + purrfindController.lastSearchMilliseconds + " ms"
                        color: window.secondaryText; font.pixelSize: 10
                    }
                    Rectangle {
                        Layout.preferredWidth: 9; Layout.preferredHeight: 9; radius: 5
                        color: purrfindController.error.length ? "#ff667f" : "#45dc73"
                    }
                    Text {
                        text: purrfindController.error.length ? purrfindController.error
                              : purrfindController.searching ? "Pesquisando..." : "Índice atualizado"
                        color: purrfindController.error.length ? "#ff8da0" : window.secondaryText
                        font.pixelSize: 10; elide: Text.ElideRight; Layout.maximumWidth: 180
                    }
                }
            }
        }
    }

    Popup {
        id: filtersPopup
        x: window.width - width - 22; y: header.height + 8
        width: 380; height: window.advancedFiltersExpanded ? 560 : 350
        padding: 18; modal: false; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 14; color: theme.surface; border.color: window.border
            layer.enabled: true
        }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Text { text: "▽"; color: window.accent; font.pixelSize: 21 }
                Text { text: "Filtros rápidos"; color: window.primaryText; font.pixelSize: 14; font.bold: true }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: "Limpar"
                    onClicked: { searchField.clear(); categoryTabs.currentIndex = 0; filtersPopup.close() }
                }
            }
            Flow {
                Layout.fillWidth: true; Layout.preferredHeight: 180; Layout.minimumHeight: 180; spacing: 8
                Repeater {
                    model: ["type:pdf", "type:docx", "type:xlsx", "type:txt",
                            "modified:today", "modified:7d", "modified:30d",
                            "size:>1MB", "size:>10MB", "category:image", "category:video"]
                    FilterChip {
                        required property string modelData
                        text: modelData
                        onClicked: { window.appendFilter(modelData); filtersPopup.close() }
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: window.border }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "◇"; color: window.accent; font.pixelSize: 18 }
                Text { text: "Filtros avançados"; color: window.primaryText; font.pixelSize: 13; font.bold: true }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: window.advancedFiltersExpanded ? "⌃" : "⌄"
                    onClicked: window.advancedFiltersExpanded = !window.advancedFiltersExpanded
                }
            }
            ColumnLayout {
                visible: window.advancedFiltersExpanded; Layout.fillWidth: true; spacing: 9
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.preferredWidth: 100; text: "Tipo de arquivo"; color: window.secondaryText; font.pixelSize: 11 }
                    FilterCombo {
                        Layout.fillWidth: true
                        model: ["Todos", "PDF", "DOCX", "XLSX", "TXT", "Imagens", "Vídeos"]
                        onActivated: function(index) {
                            if (index > 0 && index < 5) window.appendFilter("type:" + currentText.toLowerCase())
                            else if (index === 5) window.appendFilter("category:image")
                            else if (index === 6) window.appendFilter("category:video")
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.preferredWidth: 100; text: "Período"; color: window.secondaryText; font.pixelSize: 11 }
                    FilterCombo {
                        Layout.fillWidth: true; model: ["Qualquer data", "Hoje", "Últimos 7 dias", "Últimos 30 dias"]
                        onActivated: function(index) { if (index > 0) window.appendFilter(["", "modified:today", "modified:7d", "modified:30d"][index]) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.preferredWidth: 100; text: "Tamanho"; color: window.secondaryText; font.pixelSize: 11 }
                    FilterCombo {
                        Layout.fillWidth: true; model: ["Qualquer tamanho", "Maior que 1 MB", "Maior que 10 MB", "Maior que 100 MB"]
                        onActivated: function(index) { if (index > 0) window.appendFilter(["", "size:>1MB", "size:>10MB", "size:>100MB"][index]) }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.preferredWidth: 100; text: "Pasta"; color: window.secondaryText; font.pixelSize: 11 }
                    TextField {
                        Layout.fillWidth: true; implicitHeight: 36; placeholderText: "Pasta indexada"
                        placeholderTextColor: window.theme.subtle; color: window.primaryText; font.pixelSize: 11
                        leftPadding: 12; rightPadding: 12
                        background: Rectangle { radius: 7; color: parent.activeFocus ? window.theme.elevated : window.theme.chip; border.color: parent.activeFocus ? window.accent : window.theme.border }
                        onAccepted: { if (text.trim().length) window.appendFilter("folder:\"" + text.trim() + "\""); clear(); filtersPopup.close() }
                    }
                }
            }
        }
    }

    Timer { id: searchDebounce; interval: 40; repeat: false; onTriggered: window.runSearch() }
    Dialog {
        id: propertiesDialog
        title: "Propriedades"; modal: true; width: 580; height: 330
        x: (window.width - width) / 2; y: (window.height - height) / 2
        standardButtons: Dialog.Close
        background: Rectangle { color: window.theme.surface; radius: 14; border.color: window.border }
        contentItem: TextArea {
            text: window.propertiesText; readOnly: true; selectByMouse: true
            wrapMode: TextEdit.WrapAnywhere; color: window.primaryText; font.pixelSize: 13; background: Item {}
        }
    }
    SettingsDialog {
        id: settings
        x: 0; y: 0; width: window.width; height: window.height
        onClosed: searchField.forceActiveFocus()
    }
    Connections {
        target: purrfindController
        function onConfigChanged() {
            try {
                let configured = JSON.parse(purrfindController.configJson).themeMode
                if (["system", "light", "dark"].indexOf(configured) >= 0)
                    window.themeMode = configured
            } catch (_) {}
        }
    }
    Component.onCompleted: {
        try {
            let configured = JSON.parse(purrfindController.configJson).themeMode
            if (["system", "light", "dark"].indexOf(configured) >= 0)
                window.themeMode = configured
        } catch (_) {}
        x = Math.round((Screen.width - width) / 2)
        y = Math.max(24, Math.round((Screen.height - height) / 2))
        searchField.forceActiveFocus()
    }
    onVisibleChanged: {
        if (visible) { searchField.forceActiveFocus(); searchField.selectAll() }
        else videoPlayer.pause()
    }
    onPreviewPanelVisibleChanged: if (!previewPanelVisible) videoPlayer.pause()
}
