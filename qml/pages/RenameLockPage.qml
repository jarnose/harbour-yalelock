import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog

    property string lockId
    property alias name: nameField.text

    canAccept: nameField.text.trim().length > 0

    onAccepted: AppController.renameLock(lockId, nameField.text)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: qsTr("Rename")
                acceptText: qsTr("Save")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }
        }
    }
}
