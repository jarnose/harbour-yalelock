import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    ListModel {
        id: foundModel
    }

    Connections {
        target: AppController
        onDeviceFound: {
            for (var i = 0; i < foundModel.count; ++i) {
                if (foundModel.get(i).address === device.address) {
                    foundModel.set(i, device)
                    return
                }
            }
            foundModel.append(device)
        }
    }

    Component.onCompleted: AppController.startScan()
    Component.onDestruction: AppController.stopScan()

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: foundModel

        header: Column {
            width: parent.width

            PageHeader {
                title: qsTr("Nearby locks")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: qsTr("Pick the lock by its Bluetooth address, then enter its name and offline "
                           + "key on the previous screen.")
            }

            BusyLabel {
                running: AppController.scanning
                text: qsTr("Scanning…")
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0 && !AppController.scanning
            text: qsTr("No locks found yet")
            hintText: qsTr("Make sure the lock is nearby and powered")
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                }

                Label {
                    text: model.name && model.name.length > 0 ? model.name : qsTr("Unnamed device")
                    truncationMode: TruncationMode.Fade
                }
                Label {
                    text: model.address + (model.paired ? qsTr(" · paired") : "")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                }
            }

            onClicked: {
                var previous = pageStack.previousPage()
                if (previous && previous.hasOwnProperty("bleAddress"))
                    previous.bleAddress = model.address
                pageStack.pop()
            }
        }

        VerticalScrollDecorator {}
    }
}
