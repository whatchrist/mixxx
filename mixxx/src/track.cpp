//
// C++ Implementation: track
//
// Description:
//
//
// Author: Tue Haste Andersen <haste@diku.dk>, (C) 2003
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include "track.h"
#include "trackinfoobject.h"
#include "trackcollection.h"
#include "xmlparse.h"
#include <qfile.h>
#include <QLabel>
#include <qcombobox.h>
#include <qlineedit.h>
//Added by qt3to4:
#include <QDropEvent>
#include "mixxxview.h"
#include <q3dragobject.h>

/*used for new model/view interface*/
#include "wtracktablemodel.h"
#include "wplaylistlistmodel.h"
#include "wtracktableview.h"
#include "libraryscanner.h"
#include "libraryscannerdlg.h"

//#include "wtreeview.h"
#include "dlgbpmtap.h"
#include "wnumberpos.h"
#include <QMenu>
#include <QCursor>
#include <q3cstring.h>
#include "enginebuffer.h"
#include "reader.h"
#include "controlobject.h"
#include "controlobjectthreadmain.h"
#include "configobject.h"
#include "trackimporter.h"
#include "wavesummary.h"
#include "bpmdetector.h"
#include "woverview.h"
#include "playerinfo.h"

#include <q3progressdialog.h>

#include <QEvent>
#include <QObject>
#include <QLineEdit>
#include <QDebug>

Track::Track(QString location, MixxxView * pView, ConfigObject<ConfigValue> *config, EngineBuffer * pBuffer1, EngineBuffer * pBuffer2, WaveSummary * pWaveSummary, BpmDetector * pBpmDetector)
{
    m_pView = pView;
    m_pBuffer1 = pBuffer1;
    m_pBuffer2 = pBuffer2;
    m_pActivePlaylist = 0;
    m_pActivePopupPlaylist = 0;
    m_pTrackPlayer1 = 0;
    m_pTrackPlayer2 = 0;
    m_pWaveSummary = pWaveSummary;
    m_pBpmDetector = pBpmDetector;
    m_pConfig = config;
    m_iLibraryIdx = 0;   //FIXME: Deprecated, can safely remove.
    m_iPlayqueueIdx = 0; //FIXME: Deprecated, can safely remove.

    m_pTrackCollection = new TrackCollection(m_pBpmDetector);
    m_pTrackImporter = new TrackImporter(m_pView,m_pTrackCollection);
    m_pLibraryModel = new WTrackTableModel(m_pView->m_pTrackTableView);
    m_pPlayQueueModel = new WTrackTableModel(m_pView->m_pTrackTableView);
    m_pPlaylistModel = new WTrackTableModel(m_pView->m_pTrackTableView);
    m_pPlaylistListModel = new WPlaylistListModel(m_pView->m_pTrackTableView); 

    //Pass the track collcetion along to the library and playqueue playlists.
    m_qLibraryPlaylist.setTrackCollection(m_pTrackCollection);
    m_qPlayqueuePlaylist.setTrackCollection(m_pTrackCollection);

    // Read the XML file
    qDebug() << "Loading playlists and library tracks from XML...";
    readXML(location);   


    m_pScanner = new LibraryScanner(&m_qLibraryPlaylist, "");
    //Refresh the tableview when the library is done being scanned. (FIXME: Is a hack)
    connect(m_pScanner, SIGNAL(scanFinished()), m_pView->m_pTrackTableView, SLOT(repaintEverything())); 
    
    //The WTrackTableView doesn't have a pointer to us yet (which it needs to retrieve the list of playlists),
    //so give it one.
    m_pView->m_pTrackTableView->setTrack(this);
     
    // Update anything that views the playlists
    updatePlaylistViews();

    // Insert the first playlist in the list
    m_pActivePlaylist = &m_qLibraryPlaylist;
    //m_pActivePlaylist->activate(m_pView->m_pTrackTable);

    m_pLibraryModel->setTrackPlaylist(&m_qLibraryPlaylist);
    m_pPlayQueueModel->setTrackPlaylist(&m_qPlayqueuePlaylist);
    m_pPlaylistListModel->setPlaylistList(&m_qPlaylists);
       
    //If the TrackCollection appears to be empty (could be first run), then scan the library
    if (m_pTrackCollection->getSize() == 0)
    {
        slotScanLibrary();
    }
    else if (checkLibraryLastModified() == true) //Check to see if the library has been modified since
                                                 //we last scanned it.
    {
        slotScanLibrary();
    }

    qDebug() << "Trying to add" << m_pTrackCollection->getSize() << "songs to the library playlist";
    for (int i = 0; i < m_pTrackCollection->getSize(); i++)
    {
        m_qLibraryPlaylist.addTrack(m_pTrackCollection->getTrack(i));
    }
  
    //Scan the music library on disk
    //slotScanLibrary();
   
    if (m_pView && m_pView->m_pTrackTableView) //Stops Mixxx from dying if a skin doesn't have the search box.
    {
        m_pView->m_pTrackTableView->setSearchSource(m_pLibraryModel);
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);

        // Connect ComboBox events to WTrackTable
        connect(m_pView->m_pComboBox, SIGNAL(activated(int)), this, SLOT(slotActivatePlaylist(int)));

        // Connect Search to table
/*
        connect( m_pView->m_pLineEditSearch,
                SIGNAL( textChanged( const QString & )),
		m_pView->m_pTrackTableView,
		SLOT(slotFilter(const QString &)));
*/
        startTimer(250);   // Update the TrackTableView filter a maximum of 4 times a second.

	// add EventFilter to do a selectAll text when the LineEditSearch gains focus
	m_pView->m_pLineEditSearch-> installEventFilter(this);

        // Connect drop events to table
        //connect(m_pView->m_pTrackTable, SIGNAL(dropped(QDropEvent *)), this, SLOT(slotDrop(QDropEvent *)));
    }
    else
    {
        //If there was no TrackTableView specified in the skin, give a warning.
        QMessageBox::warning(NULL, tr("Mixxx"),
                             tr("You're using a skin that is incompatible with Mixxx " + QString(VERSION) + ", which "
                             "will cause unexpected behaviour (eg. missing library).\nThis can happen if you're "
                             "using a third-party skin or if you've incorrectly upgraded Mixxx."), 
                             QMessageBox::Ok, QMessageBox::Ok);

    }


    // Get ControlObject for determining end of track mode, and set default value to STOP.
    m_pEndOfTrackModeCh1 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel1]","TrackEndMode")));
    m_pEndOfTrackModeCh2 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel2]","TrackEndMode")));

    // Connect end-of-track signals to this object
    m_pEndOfTrackCh1 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel1]","TrackEnd")));
    m_pEndOfTrackCh2 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel2]","TrackEnd")));
    connect(m_pEndOfTrackCh1, SIGNAL(valueChanged(double)), this, SLOT(slotEndOfTrackPlayer1(double)));
    connect(m_pEndOfTrackCh2, SIGNAL(valueChanged(double)), this, SLOT(slotEndOfTrackPlayer2(double)));

    // Get play buttons
    m_pPlayButtonCh1 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel1]","play")));
    m_pPlayButtonCh2 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel2]","play")));

    // Play position for each player. Used to determine which track to load next
    m_pPlayPositionCh1 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel1]","playposition")));
    m_pPlayPositionCh2 = new ControlObjectThreadMain(ControlObject::getControl(ConfigKey("[Channel2]","playposition")));

    // Make controls for next and previous track
    m_pNextTrackCh1 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel1]","NextTrack")));
    m_pPrevTrackCh1 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel1]","PrevTrack")));
    m_pNextTrackCh2 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel2]","NextTrack")));
    m_pPrevTrackCh2 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel2]","PrevTrack")));
    connect(m_pNextTrackCh1, SIGNAL(valueChanged(double)), this, SLOT(slotNextTrackPlayer1(double)));
    connect(m_pPrevTrackCh1, SIGNAL(valueChanged(double)), this, SLOT(slotPrevTrackPlayer1(double)));
    connect(m_pNextTrackCh2, SIGNAL(valueChanged(double)), this, SLOT(slotNextTrackPlayer2(double)));
    connect(m_pPrevTrackCh2, SIGNAL(valueChanged(double)), this, SLOT(slotPrevTrackPlayer2(double)));

    // Make controls for tracklist navigation and current track loading
    m_pLoadSelectedTrackCh1 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel1]","LoadSelectedTrack")));
    m_pLoadSelectedTrackCh2 = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Channel2]","LoadSelectedTrack")));
    m_pSelectNextTrack = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Playlist]","SelectNextTrack")));
    m_pSelectPrevTrack = new ControlObjectThreadMain(new ControlObject(ConfigKey("[Playlist]","SelectPrevTrack")));
    connect(m_pLoadSelectedTrackCh1, SIGNAL(valueChanged(double)), this, SLOT(slotLoadSelectedTrackCh1(double)));
    connect(m_pLoadSelectedTrackCh2, SIGNAL(valueChanged(double)), this, SLOT(slotLoadSelectedTrackCh2(double)));
    connect(m_pSelectNextTrack, SIGNAL(valueChanged(double)), this, SLOT(slotSelectNextTrack(double)));
    connect(m_pSelectPrevTrack, SIGNAL(valueChanged(double)), this, SLOT(slotSelectPrevTrack(double)));

    TrackPlaylist::setTrack(this);

	m_pView->m_pTrackTableView->repaintEverything();

}

Track::~Track()
{
}

void Track::timerEvent(QTimerEvent *event) {
  if (m_pView->m_pLineEditSearch->isModified()) {        
    m_pView->m_pTrackTableView->slotFilter(m_pView->m_pLineEditSearch->text()); // set the library filter
    m_pView->m_pLineEditSearch->setText(m_pView->m_pLineEditSearch->text()); // reset the isModified flag
    // qDebug() << "isModified =" << m_pView->m_pLineEditSearch->isModified();
  }
}

bool Track::eventFilter(QObject *obj, QEvent *e) {
  if (obj == m_pView->m_pLineEditSearch) {
    qDebug() << "Track::eventFilter: Received event:" << e->type();

    switch (e->type()) { 
      case QEvent::MouseButtonPress:  // Drop entry events which deselect the text
      case QEvent::MouseButtonRelease:
        // qDebug("Drop mouse event.");
        return true;
        break;
      case QEvent::FocusIn: 
        // qDebug("QEvent::FocusIn event intercepted");
        ((QLineEdit *)obj)->selectAll();
        // qDebug() << "hasSelectedText? " << ((QLineEdit *)obj)->hasSelectedText();
        // qDebug() << "So the selected text is now: " << ((QLineEdit *)obj)->selectedText();
        break;
    }
  }
  return false;
}

void Track::readXML(QString location)
{
    qDebug() << "Track::readXML" << location;
    
    // Open XML file
    QFile file(location);
    QDomDocument domXML("Mixxx_Track_List");

    // Check if we can open the file
    if (!file.exists())
    {
        qDebug() << "Track:" << location <<  "does not exist.";
        file.close();
        return;
    }

    // Check if there is a parsing problem
    QString error_msg;
    int error_line;
    int error_column;
    if (!domXML.setContent(&file, &error_msg, &error_line, &error_column))
    {
        qDebug() << "Track: Parse error in" << location;
        qDebug() << "Doctype:" << domXML.doctype().name();
        qDebug() << error_msg << "on line" << error_line << ", column" << error_column;
        file.close();
        return;
    }

    file.close();

    // Get the root element
    QDomElement elementRoot = domXML.documentElement();

    // Get version
    //int version = XmlParse::selectNodeInt(elementRoot, "Version");

    // Initialize track collection
    QDomNode node = XmlParse::selectNode(elementRoot, "TrackList");
    m_pTrackCollection->readXML(node);

    qDebug("Break");

    // Get all the Playlists written in the xml file:
    node = XmlParse::selectNode(elementRoot, "Playlists").firstChild();
    QString qPlaylistName; //Name of the current playlist
    while (!node.isNull())
    {
        if (node.isElement() && node.nodeName()=="Playlist")
        {
            //Create the playlists internally.
            //If the playlist is "Library" or "Play Queue", insert it into
            //a special spot in the list of playlists.
            qPlaylistName = XmlParse::selectNodeQString(node, "Name");
            if (qPlaylistName == "Library")
            {
                m_qLibraryPlaylist.setTrackCollection(m_pTrackCollection);
                m_qLibraryPlaylist.loadFromXMLNode(node);
            }
            else if (qPlaylistName == "Play Queue")
            {
                m_qPlayqueuePlaylist.setTrackCollection(m_pTrackCollection);
                m_qPlayqueuePlaylist.loadFromXMLNode(node);
            }
            else
                m_qPlaylists.append(new TrackPlaylist(m_pTrackCollection, node));
        }
        

        node = node.nextSibling();
    }
}

void Track::writeXML(QString location)
{
    Q3ProgressDialog progress( "Writing song database...", 0, m_qPlaylists.count()+5,
                              0, "progress", TRUE );
    progress.show();
    int i = 0;

    // Create the xml document:
    QDomDocument domXML( "Mixxx_Track_List" );

    // Ensure UTF16 encoding
    domXML.appendChild(domXML.createProcessingInstruction("xml","version=\"1.0\" encoding=\"UTF-16\""));

    // Set the document type
    QDomElement elementRoot = domXML.createElement( "Mixxx_Track_List" );
    domXML.appendChild(elementRoot);

    // Add version information:
    XmlParse::addElement(domXML, elementRoot, "Version", QString("%1").arg(TRACK_VERSION));

    progress.setProgress(++i);
    qApp->processEvents();

    // Write collection of tracks
    m_pTrackCollection->writeXML(domXML, elementRoot);

    progress.setProgress(++i);
    qApp->processEvents();

    // Write playlists
    QDomElement playlistsroot = domXML.createElement("Playlists");

    QListIterator<TrackPlaylist*> it(m_qPlaylists);
    TrackPlaylist* current;
    while (it.hasNext())
    {
        current = it.next();
        progress.setProgress(++i);
        qApp->processEvents();

        QDomElement elementNew = domXML.createElement("Playlist");
        current->writeXML(domXML, elementNew);
        playlistsroot.appendChild(elementNew);

    }
    elementRoot.appendChild(playlistsroot);

    progress.setProgress(++i);
    qApp->processEvents();

    // Open the file:
    QFile opmlFile(location);
    if (!opmlFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(0,
                              tr("Error"),
                              tr("Cannot open file %1").arg(location));
        return;
    }

    progress.setProgress(++i);
    qApp->processEvents();

    // Write to the file:
    Q3TextStream Xml(&opmlFile);
    Xml.setEncoding(Q3TextStream::Unicode);
    Xml << domXML.toString();
    opmlFile.close();

    progress.setProgress(++i);
    qApp->processEvents();

}

TrackCollection * Track::getTrackCollection()
{
    return m_pTrackCollection;
}


void Track::slotScanLibrary()
{
    qDebug() << "Starting Library Scanner...";
    m_pScanner->scan(m_pConfig->getValueString(ConfigKey("[Playlist]","Directory")));  
    
    //Get the last modified timestamp for the library directory.
    QFileInfo libDir(m_pConfig->getValueString(ConfigKey("[Playlist]","Directory")));
    QString lastModified = libDir.lastModified().toString();
    //... and save that timestamp so we can tell we can know that we need to rescan 
    //the library when that timestamp has been changed.
    m_pConfig->set(ConfigKey("[Playlist]","LastModified"), lastModified);
}


void Track::slotDrop(QDropEvent * e)
{
    qDebug("track drop");

    QString name;
#ifndef QT3_SUPPORT
    Q3CString type("playlist");
    if (!Q3TextDrag::decode(e, name, type))
#else
    if (!e->mimeData()->hasFormat("playlist"))
#endif
    {
        e->ignore();
        return;
    }

#ifdef QT3_SUPPORT
    name = e->mimeData()->text();
#endif

    e->accept();

    qDebug("name %s",name.latin1());

    slotActivatePlaylist(name);
}


/* CTAF: I dont think this function is used actually */
void Track::slotActivatePlaylist(QString name)
{
/*    qDebug("playlist change\n");
    // Get pointer to requested playlist
    TrackPlaylist * pNewlist = getPlaylist(name);

    if (pNewlist)
    {
        // Deactivate current playlist
        //if (m_pActivePlaylist)
        //    m_pActivePlaylist->deactivate();
        // Activate new playlist
        m_pActivePlaylist = pNewlist;
        m_pLibraryModel->setTrackPlaylist(m_pActivePlaylist);
        //m_pActivePlaylist->activate(m_pView->m_pTrackTable);
        //m_pActivePlaylist = m_qPlaylists.at(index);
        emit(activePlaylist(pNewlist));
    }*/
}

/** Displays the track listing from a particular playlist in the tracktable view */
void Track::slotShowPlaylist(TrackPlaylist* playlist)
{
    m_pPlaylistModel->setTrackPlaylist(playlist);
    m_pActivePlaylist = playlist;
    m_pView->m_pTrackTableView->setSearchSource(m_pPlaylistModel);
    
    //We want the same behaviour out of the track table as when it's showing
    //the play queue:
    m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_PLAYQUEUE);
}

TrackPlaylist* Track::getPlaylistByIndex(int index)
{
    return m_qPlaylists.at(index);
}

TrackPlaylistList* Track::getPlaylists()
{
    return &m_qPlaylists;
}

void Track::slotActivatePlaylist(int index)
{   
    //Toggled by the ComboBox - This needs to be reorganized...
    switch(index)
    {
    case 0: //Library view
        m_pView->m_pTrackTableView->reset();
        m_pView->m_pTrackTableView->setSearchSource(m_pLibraryModel);
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);
        m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_LIBRARY);
        m_pActivePlaylist = &m_qLibraryPlaylist;
        break;
    case 1: //Play queue view
        m_pView->m_pTrackTableView->reset();
        m_pView->m_pTrackTableView->setSearchSource(m_pPlayQueueModel);
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);
        m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_PLAYQUEUE);
        m_pActivePlaylist = &m_qPlayqueuePlaylist;
        break;
    case 2: //Browse mode view
        m_pView->m_pTrackTableView->reset();
        m_pView->m_pTrackTableView->setDirModel();
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);
        m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_BROWSE);
        return;
        break;
    case 3: //Playlist List Model
        m_pView->m_pTrackTableView->reset();
        m_pView->m_pTrackTableView->setPlaylistListModel(m_pPlaylistListModel);
        //m_pView->m_pTrackTableView->setSearchSource(m_pPlaylistListModel); //Doesn't work right yet
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);
        m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_PLAYLISTS);
        //FIXME ... return here or something? - Albert
        break;
    default: // ???
        m_pView->m_pTrackTableView->reset();
        m_pView->m_pTrackTableView->setSearchSource(m_pPlayQueueModel);
        m_pView->m_pTrackTableView->resizeColumnsToContents();
        m_pView->m_pTrackTableView->setTrack(this);
        m_pView->m_pTrackTableView->setTableMode(TABLE_MODE_LIBRARY); //??
        // Insert playlist according to ComboBox index
        m_pActivePlaylist = m_qPlaylists.at(index);        
        
        //What the hell is this snippet supposed to do?
        /*
        int i = 0;
        TrackInfoObject* current_track = NULL;
        do
        {
            current_track = m_pActivePlaylist->getTrackAt(i);
            if (current_track != NULL)
                slotSendToPlayqueue(current_track);
            i++;
        } while (current_track != NULL);
        */
        //TODO: Revise these TODOs (they're wrong now probably) - Albert.
        //TODO: If the play queue is empty, load the playlist into the play queue
        //TODO: If the play queue isn't empty, ask the user whether to overwrite the play queue
        //      or append the playlist to the queue.
    }
    //if (m_pActivePlaylist)
    //m_pActivePlaylist->deactivate();


    //m_pActivePlaylist->activate(m_pView->m_pTrackTable);
}
void Track::slotNewPlaylist()
{
    // Find valid name for new playlist
    int i = 1;
    while (getPlaylist(QString("Default %1").arg(i)))
        ++i;

    TrackPlaylist * p = new TrackPlaylist(m_pTrackCollection, QString("Default %1").arg(i));
    m_qPlaylists.append(p);

    // Make the new playlist active
    //slotActivatePlaylist(p->getListName());

    // Update anything that views the playlists
    updatePlaylistViews();
}

void Track::slotDeletePlaylist(QString qName)
{
    // Is set to true if we need to activate another playlist after this one
    // has been removed
    bool bActivateOtherList = false;

    TrackPlaylist * list = getPlaylist(qName);
    if (list)
    {
        // If the deleted list is the active list...
        if (list==m_pActivePlaylist)
        {
            // Deactivate the list
            //list->deactivate();
            m_pActivePlaylist = 0;
            bActivateOtherList = true;
        }
        m_qPlaylists.remove(list);
        delete list;
    }

    if (bActivateOtherList)
    {
        if (m_qPlaylists.count()==0)
            slotNewPlaylist();

        slotActivatePlaylist(m_qLibraryPlaylist.getListName());
    }

    updatePlaylistViews();
}

void Track::slotImportPlaylist()
{
    // Find valid name for new playlist
    int i = 1;
    while (getPlaylist(QString("Imported %1").arg(i)))
        ++i;

    QString sPlsname(QString("Imported %1").arg(i));
    TrackPlaylist * pTempPlaylist = m_pTrackImporter->importPlaylist(sPlsname);

    if (pTempPlaylist != NULL)
    {
        m_qPlaylists.append(pTempPlaylist);
    }
    updatePlaylistViews();
}

TrackPlaylist * Track::getPlaylist(QString qName)
{
    QListIterator<TrackPlaylist*> it(m_qPlaylists);
    TrackPlaylist* current;
    while (it.hasNext())
    {
        current = it.next();
        if (current->getListName()==qName)
            return current;
    }
    return 0;
}

/** Sends a playlist to the play queue */
void Track::slotSendToPlayqueue(TrackPlaylist* playlist)
{
    for (int i = 0; i < playlist->getSongNum(); i++)
    {
        m_qPlayqueuePlaylist.addTrack(playlist->getTrackAt(i));
    }    
}

void Track::slotSendToPlayqueue(TrackInfoObject * pTrackInfoObject)
{
    m_qPlayqueuePlaylist.addTrack(pTrackInfoObject);
}


void Track::slotSendToPlayqueue(QString filename)
{
    TrackInfoObject* pTrack = m_pTrackCollection->getTrack(filename);
    if (pTrack)
        m_qPlayqueuePlaylist.addTrack(pTrack);
}

void Track::slotSendToPlaylist(TrackPlaylist* playlist, TrackInfoObject* pTrackInfoObject)
{
    playlist->addTrack(pTrackInfoObject);
}

void Track::slotSendToPlaylist(TrackPlaylist* playlist, QString filename)
{    
    TrackInfoObject* pTrack = m_pTrackCollection->getTrack(filename);
    if (pTrack)
        playlist->addTrack(pTrack);
}

void Track::slotLoadPlayer1(TrackInfoObject * pTrackInfoObject, bool bStartFromEndPos)
{

    QString filename = pTrackInfoObject->getLocation();
    if (!filename.isEmpty())
    {
        // Check if filename is valid
        QFileInfo finfo(filename);
        if (!finfo.exists())
        {
            qDebug() << "Song in library not found on at" << filename << "on disk.";
            qDebug() << "Removing from library...";
            m_pTrackCollection->removeTrack(pTrackInfoObject); //Remove the track from the library.
            m_pView->m_pTrackTableView->repaintEverything(); //Refresh the library view.
            return;
        }
    }

    if (m_pTrackPlayer1)
    {
        m_pTrackPlayer1->setOverviewWidget(0);
        m_pTrackPlayer1->setBpmControlObject(0);
    }

    m_pTrackPlayer1 = pTrackInfoObject;

    // Update score:
    m_pTrackPlayer1->incTimesPlayed();
    if (m_pActivePlaylist)
        m_pActivePlaylist->updateScores();

    // Request a new track from the reader:
    m_pBuffer1->getReader()->requestNewTrack(m_pTrackPlayer1, bStartFromEndPos);

    // Detect BPM if required
    if (m_pTrackPlayer1->getBpmConfirm()== false || m_pTrackPlayer1->getBpm() == 0.)
        m_pTrackPlayer1->sendToBpmQueue();

    // Generate waveform summary
    if ((m_pWaveSummary && (m_pTrackPlayer1->getWaveSummary()==0 || m_pTrackPlayer1->getWaveSummary()->size()==0)))
        m_pWaveSummary->enqueue(m_pTrackPlayer1);

    // Set waveform summary display
    m_pTrackPlayer1->setOverviewWidget(m_pView->m_pOverviewCh1);

    // Set control for beat start position for use in EngineTemporal and
    // VisualTemporalBuffer. HACK.
    ControlObject * p = ControlObject::getControl(ConfigKey("[Channel1]","temporalBeatFirst"));
    if (p)
        p->queueFromThread(m_pTrackPlayer1->getBeatFirst());

    // Set Engine file BPM and duration ControlObjects
    m_pTrackPlayer1->setBpmControlObject(ControlObject::getControl(ConfigKey("[Channel1]","file_bpm")));
    m_pTrackPlayer1->setDurationControlObject(ControlObject::getControl(ConfigKey("[Channel1]","duration")));

    // Set duration in playpos widget
//    if (m_pView->m_pNumberPosCh1)
//        m_pView->m_pNumberPosCh1->setDuration(m_pTrackPlayer1->getDuration());

    // Write info to text display
    if (m_pView->m_pTextCh1)
        m_pView->m_pTextCh1->setText(m_pTrackPlayer1->getInfo());

    emit(newTrackPlayer1(m_pTrackPlayer1));

    // Update TrackInfoObject of the helper class
    PlayerInfo::Instance().setTrackInfo(1, m_pTrackPlayer1);
}

void Track::slotLoadPlayer2(TrackInfoObject * pTrackInfoObject, bool bStartFromEndPos)
{
    if (m_pTrackPlayer2)
    {
        m_pTrackPlayer2->setOverviewWidget(0);
        m_pTrackPlayer2->setBpmControlObject(0);
    }

    m_pTrackPlayer2 = pTrackInfoObject;

    // Update score:
    m_pTrackPlayer2->incTimesPlayed();
    if (m_pActivePlaylist)
        m_pActivePlaylist->updateScores();

    // Request a new track from the reader:
    m_pBuffer2->getReader()->requestNewTrack(m_pTrackPlayer2, bStartFromEndPos);

    // Detect BPM if required
    if (m_pTrackPlayer2->getBpmConfirm()== false || m_pTrackPlayer2->getBpm() == 0.)
        m_pTrackPlayer2->sendToBpmQueue();

    // Generate waveform summary
    if ((m_pWaveSummary && (m_pTrackPlayer2->getWaveSummary()==0 || m_pTrackPlayer2->getWaveSummary()->size()==0)))
        m_pWaveSummary->enqueue(m_pTrackPlayer2);

    // Set waveform summary display
    m_pTrackPlayer2->setOverviewWidget(m_pView->m_pOverviewCh2);
    m_pTrackPlayer2->setDurationControlObject(ControlObject::getControl(ConfigKey("[Channel2]","duration")));

    // Set control for beat start position for use in EngineTemporal and
    // VisualTemporalBuffer. HACK.
    ControlObject * p = ControlObject::getControl(ConfigKey("[Channel2]","temporalBeatFirst"));
    if (p)
        p->queueFromThread(m_pTrackPlayer2->getBeatFirst());

    // Set Engine file BPM ControlObject
    m_pTrackPlayer2->setBpmControlObject(ControlObject::getControl(ConfigKey("[Channel2]","file_bpm")));

    // Set duration in playpos widget
//    if (m_pView->m_pNumberPosCh2)
//        m_pView->m_pNumberPosCh2->setDuration(m_pTrackPlayer2->getDuration());

    // Write info to text display
    if (m_pView->m_pTextCh2)
        m_pView->m_pTextCh2->setText(m_pTrackPlayer2->getInfo());

    emit(newTrackPlayer2(m_pTrackPlayer2));

    // Update TrackInfoObject of the helper class
    PlayerInfo::Instance().setTrackInfo(2, m_pTrackPlayer2);
}

void Track::slotLoadPlayer1(QString filename, bool bStartFromEndPos)
{
    TrackInfoObject * pTrack = m_pTrackCollection->getTrack(filename);
    if (pTrack)
        slotLoadPlayer1(pTrack, bStartFromEndPos);
}

void Track::slotLoadPlayer2(QString filename, bool bStartFromEndPos)
{
    TrackInfoObject * pTrack = m_pTrackCollection->getTrack(filename);
    if (pTrack)
        slotLoadPlayer2(pTrack, bStartFromEndPos);
}

TrackPlaylist * Track::getActivePlaylist()
{
    return m_pActivePlaylist;
}

void Track::slotEndOfTrackPlayer1(double val)
{
//    qDebug("end of track %f",val);

    if (val==0.)
        return;

    switch ((int)m_pEndOfTrackModeCh1->get())
    {
    case TRACK_END_MODE_NEXT:
        if (m_pTrackPlayer1)
        {
            TrackInfoObject * pTrack;
            bool bStartFromEndPos = false;

            //Load the next song...
            if (m_pPlayPositionCh1->get()>0.5)
            {
               //If the play queue has another song in it, load that...
               pTrack = m_pTrackPlayer1->getNext(&m_qPlayqueuePlaylist);
               
               
               /* Disabled because we only ever want to pull from the play queue:
               
               //Otherwise load from the active playlist...
               if (!pTrack && m_pActivePlaylist)
                    pTrack = m_pTrackPlayer1->getNext(m_pActivePlaylist);
               
               //Fall back on getting the next song from Browse mode.
               if (!pTrack)
               {
                    //If we're in Browse mode...
                    //qDebug() << "Browse-mode NEXT player 1";
                    pTrack = NULL;
                    QString qNextTrackPath = m_pView->m_pTrackTableView->getNextTrackBrowseMode(m_pTrackPlayer1);
                    slotLoadPlayer1(qNextTrackPath);
               }
               */
            }
            else //Load previous track
            {
                pTrack = m_pTrackPlayer1->getPrev(m_pActivePlaylist);
                
                /* Only pull from play queue instead!
                
                //Fall back on getting the prev song from Browse mode.
                if (!pTrack)
                {
                    //If we're in Browse mode...
                    pTrack = NULL;
                    QString qPrevTrackPath = m_pView->m_pTrackTableView->getPrevTrackBrowseMode(m_pTrackPlayer1);
                    slotLoadPlayer1(qPrevTrackPath, true);
                }
                */         
                
                bStartFromEndPos = true;
            }

            if (bStartFromEndPos)
                qDebug("start from end");

            if (pTrack)
                slotLoadPlayer1(pTrack, bStartFromEndPos);
            else
            {
//                 m_pPlayButtonCh1->slotSet(0.);
                m_pEndOfTrackCh1->slotSet(0.);
            }
        }
        break;
    }
    //m_pEndOfTrackCh1->slotSet(0.);
}

void Track::slotEndOfTrackPlayer2(double val)
{
    if (val==0.)
        return;

    switch ((int)m_pEndOfTrackModeCh2->get())
    {
    case TRACK_END_MODE_NEXT:
        if (m_pTrackPlayer2)
        {
            TrackInfoObject * pTrack;
            bool bStartFromEndPos = false;

            if (m_pPlayPositionCh2->get()>0.5)
            {
               //If the play queue has another song in it, load that...
               pTrack = m_pTrackPlayer2->getNext(&m_qPlayqueuePlaylist);
               
               
               /* Disabled because we only ever want to pull from the play queue:
               
               //Otherwise load from the active playlist... 
               if (!pTrack && m_pActivePlaylist)
                    pTrack = m_pTrackPlayer2->getNext(m_pActivePlaylist);
               
               //Fall back on getting the next song from Browse mode.
               if (!pTrack)
               {
                    //If we're in Browse mode...
                    //qDebug() << "Browse-mode NEXT player 1";
                    pTrack = NULL;
                    QString qNextTrackPath = m_pView->m_pTrackTableView->getNextTrackBrowseMode(m_pTrackPlayer2);
                    slotLoadPlayer2(qNextTrackPath);
               }
               */
               
            }
            else //Load previous track
            {
                pTrack = m_pTrackPlayer2->getPrev(m_pActivePlaylist);
                
                /* Only pull from play queue instead!
                
                //Fall back on getting the prev song from Browse mode.
                if (!pTrack)
                {
                    //If we're in Browse mode...
                    pTrack = NULL;
                    QString qPrevTrackPath = m_pView->m_pTrackTableView->getPrevTrackBrowseMode(m_pTrackPlayer2);
                    slotLoadPlayer2(qPrevTrackPath, true);
                }*/
                
                bStartFromEndPos = true;
            }

            if (pTrack)
                slotLoadPlayer2(pTrack, bStartFromEndPos);
            else
            {
//                 m_pPlayButtonCh2->slotSet(0.);
                m_pEndOfTrackCh2->slotSet(0.);
            }
        }
        break;
    }
    //m_pEndOfTrackCh2->slotSet(0.);
}

void Track::slotLoadSelectedTrackCh1(double v)
{
    QModelIndex index;
    TrackInfoObject *pTrack;
    // Only load on key presses and if we're not in browse mode
    if (v && m_pView->m_pTrackTableView->m_pTable)
    {
        // Fetch the currently selected track 
        index = m_pView->m_pTrackTableView->currentIndex();
        pTrack = m_pView->m_pTrackTableView->m_pTable->m_pTrackPlaylist->getTrackAt(index.row());
	// If there is one, load it
	if (pTrack) slotLoadPlayer1(pTrack);
    }
}

void Track::slotLoadSelectedTrackCh2(double v)
{
    QModelIndex index;
    TrackInfoObject *pTrack;
    // Only load on key presses and if we're not in browse mode
    if (v && m_pView->m_pTrackTableView->m_pTable) {
        // Fetch the currently selected track 
        index = m_pView->m_pTrackTableView->currentIndex();
        pTrack = m_pView->m_pTrackTableView->m_pTable->m_pTrackPlaylist->getTrackAt(index.row());
	// If there is one, load it
        if (pTrack) slotLoadPlayer2(pTrack);
    }
}

void Track::slotSelectNextTrack(double v)
{
    // Only move on key presses
    if (v) m_pView->m_pTrackTableView->selectNext();
}

void Track::slotSelectPrevTrack(double v)
{
    // Only move on key presses
    if (v) m_pView->m_pTrackTableView->selectPrevious();
}

void Track::slotNextTrackPlayer1(double v)
{
    if (v && m_pTrackPlayer1)
    {
        TrackInfoObject * pTrack = m_pTrackPlayer1->getNext(m_pActivePlaylist);
        if (pTrack)
            slotLoadPlayer1(pTrack);
    }
}

void Track::slotPrevTrackPlayer1(double v)
{
    if (v && m_pTrackPlayer1)
    {
        TrackInfoObject * pTrack = m_pTrackPlayer1->getPrev(m_pActivePlaylist);
        if (pTrack)
            slotLoadPlayer1(pTrack);
    }
}

void Track::slotNextTrackPlayer2(double v)
{
    if (v && m_pTrackPlayer2)
    {
        TrackInfoObject * pTrack = m_pTrackPlayer2->getNext(m_pActivePlaylist);
        if (pTrack)
            slotLoadPlayer2(pTrack);
    }
}

void Track::slotPrevTrackPlayer2(double v)
{
    if (v && m_pTrackPlayer2)
    {
        TrackInfoObject * pTrack = m_pTrackPlayer2->getPrev(m_pActivePlaylist);
        if (pTrack)
            slotLoadPlayer2(pTrack);
    }
}

void Track::updatePlaylistViews()
   {
    // Sort list
    qSort(m_qPlaylists);
    
    //Tell the track table view to update it's right-click/send-to-playlist menu.
    m_pView->m_pTrackTableView->updatePlaylistActions();

    qDebug() << "FIXME: Need to tell the m_pPlaylistListModel to refresh in" << __FILE__ << "on line:" << __LINE__;

    // Update tree view
    //if (m_pView->m_pTreeView)
    //    m_pView->m_pTreeView->updatePlaylists(&m_qPlaylists);
    
    //m_pPlayQueueModel->setTrackPlaylist(m_qPlaylists)
    
    // Update menu
    //emit(updateMenu(&m_qPlaylists));

    // Set active
    if (m_pActivePlaylist)
        emit(activePlaylist(m_pActivePlaylist));
   }

/** Checks if the library directory's "last modified" timestamp has been changed */
/* @returns true if the library has been modified since it was last rescanned
   @returns false if the library has not been modified since it was last rescanned 
*/
bool Track::checkLibraryLastModified()
{
    //Get some info about the library directory
    QFileInfo libDir(m_pConfig->getValueString(ConfigKey("[Playlist]","Directory")));
    
    //Get the last modified timestamp for the library directory.
    QString lastModified = libDir.lastModified().toString();
    
    //Compare the timestamp with our own stored timestamp, to see if the library
    //has been modified since we last scanned it.
    if (lastModified == m_pConfig->getValueString(ConfigKey("[Playlist]","LastModified")))
    {
        return false;
    }
    else
    {
        //Note: The LastModified config key gets updated in slotScanLibrary(), for consistency.
           
        return true;
    }
}

/** Runs the BPM detection on every track in the TrackCollection that doesn't already have a BPM. */
void Track::slotBatchBPMDetection()
{
/*
    //Unfinished/WIP - Albert Jan 31/08 
    //General idea should work more or less, I suppose...

    //Iterate over each TrackInfoObject stored in m_pTrackCollection
    TrackInfoObject* cur_track;
    QListIterator<TrackCollection*> it(m_qPlaylists);
    TrackPlaylist* current;
    while (it.hasNext())
    {
        current = it.next();
        if (current->getListName()==qName)
            return current;
    }

    //Check to see if the TIO already has a non-zero BPM

    //If not, run TIO->sendToBpmQueue().

*/
}

