import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    property alias bleAddress: addressField.text

    readonly property bool _validKey: /^[0-9a-fA-F]{32}$/.test(keyField.text)
    readonly property int _keyIndexValue: parseInt(keyIndexField.text, 10)
    readonly property bool _validKeyIndex: !isNaN(_keyIndexValue) && _keyIndexValue >= 0 && _keyIndexValue <= 255

    canAccept: nameField.text.trim().length > 0 && addressField.text.trim().length > 0
               && _validKey && _validKeyIndex

    onAccepted: AppController.addLock(nameField.text, addressField.text, keyField.text, _keyIndexValue)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: qsTr("Add lock")
                acceptText: qsTr("Save")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: qsTr("The offline key and key index come from your Yale Home account the same "
                           + "way they do for Home Assistant's Yale Access Bluetooth integration: "
                           + "captured once from your own app traffic, or from wherever you already "
                           + "keep them. This app never talks to Yale's cloud service.")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                placeholderText: qsTr("e.g. Front door")
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: addressField.focus = true
            }

            TextField {
                id: addressField
                width: parent.width
                label: qsTr("Bluetooth address")
                placeholderText: qsTr("AA:BB:CC:DD:EE:FF")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: keyField.focus = true

                MouseArea {
                    anchors.fill: parent
                    onClicked: pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
                    enabled: addressField.text.length === 0
                }
            }

            ButtonLayout {
                Button {
                    text: qsTr("Scan for locks")
                    onClicked: pageStack.push(Qt.resolvedUrl("ScanPage.qml"))
                }
            }

            TextField {
                id: keyField
                width: parent.width
                label: qsTr("Offline key (32 hex characters)")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: keyIndexField.focus = true
                color: keyField.text.length > 0 && !dialog._validKey ? Theme.errorColor : Theme.primaryColor
            }

            TextField {
                id: keyIndexField
                width: parent.width
                label: qsTr("Key index (0-255)")
                text: "0"
                inputMethodHints: Qt.ImhDigitsOnly
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                color: !dialog._validKeyIndex ? Theme.errorColor : Theme.primaryColor
            }
        }
    }
}
