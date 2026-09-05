import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: dialog
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    background: Rectangle { color: colors.window; radius: 16; border.color: colors.border }
    property var config: ({})
    property string addTarget: "included"
    property var pendingConfig: ({})
    readonly property bool darkTheme: window.darkTheme
    readonly property var colors: window.theme

    // The controller already keeps the current configuration in configJson.
    // Reloading it while opening the dialog could race with a just-saved theme
    // and reset the selection back to the system default.
    onOpened: applyConfig()

    function applyConfig() {
        try { config = JSON.parse(purrfindController.configJson) } catch (_) { config = {} }
        includedModel.clear(); excludedModel.clear(); contentExcludedModel.clear(); ocrExcludedModel.clear()
        let included = config.includedPaths || []
        let excluded = config.excludedPaths || []
        for (let path of included) includedModel.append({"path": path})
        for (let path of excluded) excludedModel.append({"path": path})
        for (let path of (config.contentExcludedPaths || [])) contentExcludedModel.append({"path": path})
        for (let path of (config.ocrExcludedPaths || [])) ocrExcludedModel.append({"path": path})
        hiddenCheck.checked = config.showHidden !== false
        resultLimit.value = config.maxResults || 100
        shortcutField.text = config.globalShortcut || "Super+F"
        let themeIndex = ["system", "light", "dark"].indexOf(config.themeMode)
        if (themeIndex >= 0) themeSelect.currentIndex = themeIndex
        contentEnabled.checked = config.contentIndexingEnabled !== false
        let types = config.contentTypes || ["txt","md","markdown","pdf","docx","xlsx","pptx","odt","ods","odp"]
        txtType.checked = types.indexOf("txt") >= 0; mdType.checked = types.indexOf("md") >= 0
        pdfType.checked = types.indexOf("pdf") >= 0; docxType.checked = types.indexOf("docx") >= 0
        xlsxType.checked = types.indexOf("xlsx") >= 0; pptxType.checked = types.indexOf("pptx") >= 0
        odtType.checked = types.indexOf("odt") >= 0; odsType.checked = types.indexOf("ods") >= 0
        odpType.checked = types.indexOf("odp") >= 0
        contentSize.value = Math.round((config.maximumContentFileBytes || 104857600) / 1048576)
        previewAutomatically.checked = config.previewAutomatically !== false
        metadataAdvanced.checked = config.advancedImageMetadata !== false
        usageRanking.checked = config.usageRankingEnabled !== false
        ocrPdf.checked = config.ocrPdfEnabled !== false
        ocrImages.checked = config.ocrImagesEnabled === true
        let ocrLanguages = config.ocrLanguages || ["por", "eng"]
        ocrLanguageModel.clear()
        for (let code of (dialog.indexStatus.ocrAvailableLanguages || [])) {
            let display = code === "por" ? "Português" : code === "eng" ? "English" : code
            ocrLanguageModel.append({"code": code, "languageLabel": display,
                                     "selected": ocrLanguages.indexOf(code) >= 0})
        }
        ocrProfile.currentIndex = Math.max(0, ["low", "normal", "high"].indexOf(config.ocrResourceProfile || "low"))
        ocrBattery.checked = config.ocrReduceOnBattery !== false
        ocrLowBattery.checked = config.ocrPauseBelowThirtyPercent !== false
        ocrPageLimit.value = config.ocrMaximumPdfPages || 100
    }

    function dataSize(bytes) {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KiB"
        return (bytes / 1048576).toFixed(1) + " MiB"
    }

    property var indexStatus: {
        try { return JSON.parse(purrfindController.statusJson) } catch (_) { return {} }
    }

    Connections {
        target: purrfindController
        function onConfigChanged() { if (!dialog.visible) dialog.applyConfig() }
    }

    function persist(updated, reprocessOcr) {
        if (purrfindController.saveConfig(JSON.stringify(updated))) {
            if (reprocessOcr) purrfindController.reindexOcr()
            dialog.close()
        }
    }

    function save() {
        let included = [], excluded = [], contentExcluded = [], ocrExcluded = [], types = [], ocrLanguages = []
        for (let i = 0; i < includedModel.count; ++i) included.push(includedModel.get(i).path)
        for (let i = 0; i < excludedModel.count; ++i) excluded.push(excludedModel.get(i).path)
        for (let i = 0; i < contentExcludedModel.count; ++i) contentExcluded.push(contentExcludedModel.get(i).path)
        for (let i = 0; i < ocrExcludedModel.count; ++i) ocrExcluded.push(ocrExcludedModel.get(i).path)
        for (let i = 0; i < ocrLanguageModel.count; ++i)
            if (ocrLanguageModel.get(i).selected) ocrLanguages.push(ocrLanguageModel.get(i).code)
        if (txtType.checked) types.push("txt"); if (mdType.checked) types.push("md", "markdown")
        if (pdfType.checked) types.push("pdf"); if (docxType.checked) types.push("docx")
        if (xlsxType.checked) types.push("xlsx"); if (pptxType.checked) types.push("pptx")
        if (odtType.checked) types.push("odt"); if (odsType.checked) types.push("ods")
        if (odpType.checked) types.push("odp")
        let updated = {version: 4, includedPaths: included, excludedPaths: excluded,
                       showHidden: hiddenCheck.checked, maxResults: resultLimit.value,
                       globalShortcut: shortcutField.text,
                       themeMode: ["system", "light", "dark"][themeSelect.currentIndex] || "system",
                       contentIndexingEnabled: contentEnabled.checked,
                       contentIndexingPaused: dialog.indexStatus.contentPaused || false,
                       contentTypes: types, contentExcludedPaths: contentExcluded,
                       maximumContentFileBytes: contentSize.value * 1048576,
                       maximumExtractedTextBytes: config.maximumExtractedTextBytes || 8388608,
                       previewAutomatically: previewAutomatically.checked,
                       advancedImageMetadata: metadataAdvanced.checked,
                       usageRankingEnabled: usageRanking.checked,
                       ocrPdfEnabled: ocrPdf.checked, ocrImagesEnabled: ocrImages.checked,
                       ocrPaused: dialog.indexStatus.ocrPaused || false,
                       ocrLanguages: ocrLanguages,
                       ocrResourceProfile: ["low", "normal", "high"][ocrProfile.currentIndex],
                       ocrReduceOnBattery: ocrBattery.checked,
                       ocrPauseBelowThirtyPercent: ocrLowBattery.checked,
                       ocrMaximumPdfPages: ocrPageLimit.value,
                       ocrMaximumPdfBytes: config.ocrMaximumPdfBytes || 524288000,
                       ocrDpi: config.ocrDpi || 200,
                       ocrPageTimeoutSeconds: config.ocrPageTimeoutSeconds || 90,
                       ocrExcludedPaths: ocrExcluded}
        let previous = (config.ocrLanguages || ["por", "eng"]).slice().sort().join("+")
        let next = ocrLanguages.slice().sort().join("+")
        if (previous !== next && (dialog.indexStatus.ocrProcessed || 0) > 0) {
            pendingConfig = updated
            languageChangeDialog.open()
        } else persist(updated, false)
    }

    FolderDialog {
        id: folderDialog
        title: addTarget === "included" ? "Add indexed folder" : "Exclude folder"
        onAccepted: {
            let path = selectedFolder.toString().replace(/^file:\/\//, "")
            if (addTarget === "included") includedModel.append({"path": decodeURIComponent(path)})
            else if (addTarget === "contentExcluded") contentExcludedModel.append({"path": decodeURIComponent(path)})
            else if (addTarget === "ocrExcluded") ocrExcludedModel.append({"path": decodeURIComponent(path)})
            else excludedModel.append({"path": decodeURIComponent(path)})
        }
    }

    ListModel { id: includedModel }
    ListModel { id: excludedModel }
    ListModel { id: contentExcludedModel }
    ListModel { id: ocrExcludedModel }
    ListModel { id: ocrLanguageModel }

    Dialog {
        id: languageChangeDialog
        title: "OCR languages changed"
        modal: true
        width: 440
        anchors.centerIn: parent
        contentItem: Label {
            text: "Reprocess files already indexed with the selected languages?"
            color: colors.text; wrapMode: Text.Wrap; padding: 18
        }
        footer: RowLayout {
            spacing: 8
            Button { text: "Keep existing OCR"; onClicked: { languageChangeDialog.close(); dialog.persist(dialog.pendingConfig, false) } }
            Button { text: "Reprocess OCR"; highlighted: true; onClicked: { languageChangeDialog.close(); dialog.persist(dialog.pendingConfig, true) } }
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Text { text: "Settings"; color: colors.text; font.pixelSize: 22; font.bold: true }
                Text { text: "Indexing, search and interface"; color: colors.subtle; font.pixelSize: 11 }
                Text { text: "PurrFind " + (Qt.application.version || "desenvolvimento"); color: colors.subtle; font.pixelSize: 10 }
            }
            Item { Layout.fillWidth: true }
            ToolButton { text: "Close"; Accessible.name: "Close settings"; onClicked: dialog.close() }
        }

        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width; spacing: 18

                GroupBox {
                    title: "Indexing"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: "Included folders"; color: colors.muted }
                        Repeater {
                            model: includedModel
                            RowLayout {
                                required property int index; required property string path
                                Layout.fillWidth: true
                                Text { text: path; color: colors.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                ToolButton { text: "Remove"; onClicked: includedModel.remove(index) }
                            }
                        }
                        Button { text: "+ Add folder"; onClicked: { addTarget = "included"; folderDialog.open() } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: colors.border }
                        Label { text: "Excluded folders"; color: colors.muted }
                        Repeater {
                            model: excludedModel
                            RowLayout {
                                required property int index; required property string path
                                Layout.fillWidth: true
                                Text { text: path; color: colors.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                ToolButton { text: "Remove"; onClicked: excludedModel.remove(index) }
                            }
                        }
                        Button { text: "+ Add exclusion"; onClicked: { addTarget = "excluded"; folderDialog.open() } }
                    }
                }

                GroupBox {
                    title: "Search"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    RowLayout {
                        anchors.fill: parent
                        CheckBox { id: hiddenCheck; text: "Show hidden files" }
                        Item { Layout.fillWidth: true }
                        Label { text: "Maximum results"; color: colors.muted }
                        SpinBox { id: resultLimit; from: 10; to: 1000; stepSize: 10 }
                    }
                }

                GroupBox {
                    title: "Interface"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Global shortcut"; color: colors.muted }
                            TextField { id: shortcutField; Layout.fillWidth: true; placeholderText: "Super+F" }
                            Label { text: "Applied on next UI start"; color: colors.subtle; font.pixelSize: 10 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Tema"; color: colors.muted }
                            ComboBox {
                                id: themeSelect
                                Layout.fillWidth: true
                                model: ["Sistema", "Claro", "Escuro"]
                                Accessible.name: "Tema da interface"
                            }
                            Label { text: "Sistema acompanha o tema do desktop"; color: colors.subtle; font.pixelSize: 10 }
                        }
                    }
                }

                GroupBox {
                    title: "Preview and metadata"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    ColumnLayout {
                        anchors.fill: parent
                        CheckBox { id: previewAutomatically; text: "Show preview automatically" }
                        CheckBox { id: metadataAdvanced; text: "Index advanced image metadata" }
                        CheckBox { id: usageRanking; text: "Use local PurrFind open history for ranking" }
                        Label { text: "Preview cache: automatic · 96 MiB RAM · 256 MiB disk"; color: colors.subtle; font.pixelSize: 10 }
                        RowLayout {
                            Button { text: "Clear preview cache"; onClicked: purrfindController.clearPreviewCache() }
                            Button { text: "Clear usage history"; onClicked: purrfindController.clearUsageHistory() }
                            Button { text: "Reindex image metadata"; onClicked: purrfindController.reindexMetadata() }
                        }
                    }
                }

                GroupBox {
                    title: "OCR"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    ColumnLayout {
                        anchors.fill: parent
                        Label {
                            text: dialog.indexStatus.ocrAvailable
                                  ? "Processing is entirely local; files never leave this computer."
                                  : "OCR support is not available in this build."
                            color: dialog.indexStatus.ocrAvailable ? "#8e8798" : "#e5a46f"
                            wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        CheckBox { id: ocrPdf; text: "Index scanned PDFs"; enabled: dialog.indexStatus.ocrAvailable === true }
                        CheckBox { id: ocrImages; text: "Index text in JPEG, PNG, TIFF and WebP images"; enabled: dialog.indexStatus.ocrAvailable === true }
                            Label { text: "OCR languages"; color: colors.muted }
                        Flow {
                            Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: ocrLanguageModel
                                CheckBox {
                                    required property int index
                                    required property string code
                                    required property string languageLabel
                                    required property bool selected
                                    text: languageLabel; checked: selected
                                    onToggled: ocrLanguageModel.setProperty(index, "selected", checked)
                                }
                            }
                            Label {
                                visible: dialog.indexStatus.ocrAvailable === true && ocrLanguageModel.count === 0
                                text: "OCR language pack not found. Install a system Tesseract language package."
                                color: "#e5a46f"; wrapMode: Text.Wrap; width: parent.width
                            }
                        }
                        Label {
                            visible: (dialog.indexStatus.ocrMissingLanguages || []).length > 0
                            text: "Configured language pack not found: "
                                  + (dialog.indexStatus.ocrMissingLanguages || []).join(", ")
                                  + ". Install the corresponding system Tesseract language package."
                            color: "#e5a46f"; wrapMode: Text.Wrap; Layout.fillWidth: true
                        }
                        RowLayout {
                            Label { text: "Resource usage"; color: colors.muted }
                            ComboBox { id: ocrProfile; model: ["Low", "Normal", "High"] }
                            Item { Layout.fillWidth: true }
                            Label { text: "Automatic PDF limit"; color: colors.muted }
                            SpinBox { id: ocrPageLimit; from: 1; to: 10000; value: 100 }
                            Label { text: "pages"; color: colors.subtle }
                        }
                        CheckBox { id: ocrBattery; text: "Reduce OCR activity when using battery" }
                        CheckBox { id: ocrLowBattery; text: "Pause OCR below 30% battery"; enabled: ocrBattery.checked }
                        Label { text: "OCR-specific excluded folders"; color: colors.muted }
                        Repeater {
                            model: ocrExcludedModel
                            RowLayout {
                                required property int index; required property string path
                                Layout.fillWidth: true
                                Text { text: path; color: colors.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                ToolButton { text: "Remove"; onClicked: ocrExcludedModel.remove(index) }
                            }
                        }
                        Button { text: "+ Exclude folder from OCR"; onClicked: { addTarget = "ocrExcluded"; folderDialog.open() } }
                        RowLayout {
                            Button {
                                text: dialog.indexStatus.ocrPaused ? "Resume OCR" : "Pause OCR"
                                enabled: dialog.indexStatus.ocrAvailable === true
                                onClicked: dialog.indexStatus.ocrPaused ? purrfindController.resumeOcr() : purrfindController.pauseOcr()
                            }
                            Button { text: "Reprocess OCR"; enabled: dialog.indexStatus.ocrAvailable === true; onClicked: purrfindController.reindexOcr() }
                            Label {
                                text: "Detected: " + (dialog.indexStatus.ocrDetected || 0)
                                      + " · Processed: " + (dialog.indexStatus.ocrProcessed || 0)
                                      + " · Pending: " + (dialog.indexStatus.ocrPending || 0)
                                      + " · Failed: " + (dialog.indexStatus.ocrFailed || 0)
                                      + " · Pages processed: " + (dialog.indexStatus.ocrPagesProcessed || 0)
                                color: colors.subtle; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap
                            }
                        }
                        Label {
                            visible: (dialog.indexStatus.ocrWaitReason || "").length > 0
                            text: "Waiting: " + (dialog.indexStatus.ocrWaitReason || "")
                            color: colors.subtle; font.pixelSize: 10
                        }
                    }
                }

                GroupBox {
                    title: "Content search"; Layout.fillWidth: true
                    label: Label { text: "Content search"; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    ColumnLayout {
                        anchors.fill: parent
                        CheckBox { id: contentEnabled; text: "Index document contents" }
                        Label { text: "Indexed formats"; color: colors.muted }
                        GridLayout {
                            columns: 5
                            CheckBox { id: txtType; text: "TXT" }
                            CheckBox { id: mdType; text: "Markdown" }
                            CheckBox { id: pdfType; text: "PDF" }
                            CheckBox { id: docxType; text: "DOCX" }
                            CheckBox { id: xlsxType; text: "XLSX" }
                            CheckBox { id: pptxType; text: "PPTX" }
                            CheckBox { id: odtType; text: "ODT" }
                            CheckBox { id: odsType; text: "ODS" }
                            CheckBox { id: odpType; text: "ODP" }
                        }
                        RowLayout {
                            Label { text: "Maximum document size"; color: colors.muted }
                            SpinBox { id: contentSize; from: 1; to: 1024; value: 100 }
                            Label { text: "MiB"; color: colors.subtle }
                        }
                        Label { text: "Content-only exclusions (names remain searchable)"; color: colors.muted }
                        Repeater {
                            model: contentExcludedModel
                            RowLayout {
                                required property int index; required property string path
                                Layout.fillWidth: true
                                Text { text: path; color: colors.text; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                ToolButton { text: "Remove"; onClicked: contentExcludedModel.remove(index) }
                            }
                        }
                        Button { text: "+ Exclude private folder content"; onClicked: { addTarget = "contentExcluded"; folderDialog.open() } }
                        RowLayout {
                            Button {
                                text: dialog.indexStatus.contentPaused ? "Resume content indexing" : "Pause content indexing"
                                onClicked: dialog.indexStatus.contentPaused ? purrfindController.resumeContent() : purrfindController.pauseContent()
                            }
                            Button { text: "Reindex content"; onClicked: purrfindController.reindexContent() }
                        }
                    }
                }

                GroupBox {
                    title: "Index"; Layout.fillWidth: true
                    label: Label { text: parent.title; color: colors.accent; font.bold: true }
                    background: Rectangle { color: colors.surface; border.color: colors.border; radius: 12; y: 10 }
                    RowLayout {
                        anchors.fill: parent
                        ColumnLayout {
                            Layout.fillWidth: true
                            Label { text: purrfindController.statusText; color: colors.muted }
                            Label {
                                text: "Database: " + dialog.dataSize(dialog.indexStatus.databaseSize || 0)
                                      + "  ·  Watches: " + (dialog.indexStatus.watchCount || 0)
                                color: colors.subtle; font.pixelSize: 10
                            }
                            Label {
                                visible: dialog.indexStatus.watchLimitReached === true
                                text: "The inotify watch limit was reached. The index may be incomplete; see troubleshooting."
                                color: "#e5a46f"; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: (dialog.indexStatus.recoveryBackup || "").length > 0
                                Layout.fillWidth: true; spacing: 8
                                Label {
                                    text: "The previous index was preserved after an integrity failure."
                                    color: "#e5a46f"; font.pixelSize: 10; wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Button { text: "Rebuild now"; onClicked: rebuildDialog.open() }
                            }
                            Label {
                                text: "Content: " + (dialog.indexStatus.contentIndexed || 0) + " indexed · "
                                      + (dialog.indexStatus.contentPending || 0) + " pending · "
                                      + (dialog.indexStatus.contentFailed || 0) + " failed"
                                color: colors.subtle; font.pixelSize: 10
                            }
                            Label {
                                text: "Metadata: " + (dialog.indexStatus.metadataIndexed || 0) + " indexed · "
                                      + (dialog.indexStatus.metadataPending || 0) + " pending · "
                                      + (dialog.indexStatus.metadataFailed || 0) + " failed"
                                color: colors.subtle; font.pixelSize: 10
                            }
                            Label {
                                text: dialog.indexStatus.lastUpdate
                                      ? "Last update: " + new Date(dialog.indexStatus.lastUpdate * 1000).toLocaleString()
                                      : "Initial indexing has not completed"
                                color: colors.subtle; font.pixelSize: 10
                            }
                        }
                        ColumnLayout {
                            Button { text: "Reindex"; onClicked: purrfindController.reindex() }
                            Button { text: "Rebuild index"; onClicked: rebuildDialog.open() }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { text: "Cancel"; onClicked: dialog.close() }
            Button { text: "Save"; highlighted: true; onClicked: dialog.save() }
        }
    }

    Dialog {
        id: rebuildDialog
        title: "Rebuild search index?"
        modal: true; anchors.centerIn: parent
        width: 500
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: purrfindController.rebuildIndex()
        contentItem: Label {
            text: "The current disposable index will be preserved for recovery and rebuilt. Your files and settings are not deleted."
            color: "#ddd6e5"; wrapMode: Text.Wrap; padding: 18
        }
    }
}
