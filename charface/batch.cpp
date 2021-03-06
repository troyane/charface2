#include <QDir>
#include <QDomDocument>
#include <QXmlStreamWriter>
#include <QImage>
#include <QUuid>
#include <QDebug>
#include <QImageWriter>
#include <iterator>

#include "batch.h"
#include "settingsmanager.h"

Batch::Batch(const QString path) :
    QObject(0)
{
    mPages = new PagesList();
    mPath = path;

    if(!mPath.length())
        mPath = SettingsManager::instance()->defaultBatchPath();

    loadFromDir(mPath);
}

Batch::~Batch()
{
    for (int i = 0; i < mPages->size(); i++)
    {
        Page *page = mPages->at(i);
        delete page;
    }
    delete mPages;
}

const QString Batch::tempPath(bool createPath = true)
{
    QDir tdir(mPath);
    QString tmpPath = tdir.absoluteFilePath("tmp");

    if(createPath)
        tdir.mkpath(tmpPath);

    return tmpPath;
}

bool Batch::isSaved()
{
    bool isAtTemp = mPath == SettingsManager::instance()->defaultBatchPath();
    bool hasNoPages = mPages->size() == 0;
    return (isAtTemp && hasNoPages) || (!isAtTemp);
}

void Batch::saveToDir(const QString &path)
{
    //
    copyBatchFilesTo(path);

    //
    QString prevPath = mPath;

    //
    mPath = path;
    saveXML();

    //remove all files
    if(prevPath == settingsManager->defaultBatchPath())
        removeAllFilesInDir(prevPath);

}

void Batch::save()
{
    saveToDir(mPath);
}

bool Batch::addFile(const QString &path, bool move)
{
    //load
    QImage *image = new QImage(path);

    //ignore invalid image
    if (image->isNull())
    {
        delete image;
        return false;
    }

    //create thumb
    QImage thumb = image->scaled(settingsManager->thumbSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(settingsManager->imageFormatForBatch());

    //generate unique file name
    QDir dir(mPath);
    QString nameWithExt;
    QString name;
    QString ext = settingsManager->imageFormatExt();
    do
    {
        name = QUuid::createUuid().toString();
        nameWithExt =  name + "." + ext;
    } while (dir.exists(nameWithExt));

    //save thumb file
    QString thumbName = name + settingsManager->thumbNamePrefix() + "." + SettingsManager::instance()->thumbFormatExt();
    thumb.save(dir.absoluteFilePath(thumbName));

    //
    bool res = false;
    if (move)
    {
        qDebug() << "Move file" << path << "to" << dir.absoluteFilePath(nameWithExt);
        res = QFile(path).rename(dir.absoluteFilePath(nameWithExt));
        qDebug() << "..."<< (res ? "ok" : "fail");
    }
    else
    {
        qDebug() << "Copy file" << path << "to" << dir.absoluteFilePath(nameWithExt);
        QImage convertedImage = image->convertToFormat(settingsManager->imageFormatForBatch());
        QImageWriter imageWriter(dir.absoluteFilePath(nameWithExt),ext.toStdString().c_str());
        imageWriter.setCompression(1);
        res = imageWriter.write(convertedImage);
        qDebug() << "..."<< (res ? "ok" : "fail");
    }

    //free up memory
    delete image;

    if (res)
    {
        Page *page = new Page(nameWithExt, thumbName);
        page->setParent(this);
        mPages->push_back(page);
        saveXML();
    }

    return res;
}

bool Batch::deletePage(int index)
{
    Page *page = mPages->at(index);

    QDir dir(mPath);
    bool res = dir.remove( page->fileName() );
    res &= dir.remove( page->thumbName() );

    PagesList::iterator it = mPages->begin();
    std::advance(it, index);
    mPages->erase(it);

    delete page;

    return res;
}

void Batch::loadFromDir(const QString &path)
{
    mPath = path;
    loadXML();
}

bool Batch::loadXML()
{
    //clear before load
    for (int i = 0; i < mPages->size(); i++)
    {
        Page *page = mPages->at(i);
        delete page;
    }
    mPages->clear();

    //
    QDir batchDir(mPath);
    QString fileName = batchDir.absoluteFilePath(batchFileName);

    //load xml
    QFile file(fileName);
    QDomDocument doc;
    doc.setContent(&file);

    //get "batch" section
    QDomElement node = doc.firstChildElement("batch");

    //batchinfo
    QDomElement elem = node.namedItem("batchinfo").toElement();

    //
    float ver = elem.attribute("version").toFloat();
    if ( ver > maxBatchVersionSupported || ver == 0)
    {
        mErrorMessage = QString("Batch version not supported: %1").arg(ver);
        return false;
    }

    //files
    elem = node.namedItem("files").toElement();

    //
    QDomElement el = elem.firstChildElement("file");
    while ( !el.isNull() )
    {
        //compose page
        Page *page = new Page();
        page->readFromDomElement(el);
        mPages->push_back(page);

        //get next page entry
        el = el.nextSiblingElement("file");
    }

    //
    return true;
}

bool Batch::saveXML()
{
    //
    QDir batchDir(mPath);
    QString fileName = batchDir.absoluteFilePath(batchFileName);
    QFile file(fileName);

    //
    if (!file.open(QIODevice::WriteOnly))
        return false;

    //xml writer
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);

    //
    writer.writeStartDocument();
    writer.writeStartElement("batch");
    {
        //
        writer.writeStartElement("batchinfo");
        {
            writer.writeAttribute("version", batchFileVersion);
        }
        writer.writeEndElement();

        //pages array
        writer.writeStartElement("files");
        {
            size_t pagesCount = mPages->size();
            for (size_t i = 0; i < pagesCount; i++)
            {
                Page *page = mPages->at(i);

                //page entry
                writer.writeStartElement("file");
                {
                    page->writeToXML(writer);
                }
                writer.writeEndElement();
            }
        }
        writer.writeEndElement();

    }
    writer.writeEndElement();
    writer.writeEndDocument();

    return true;
}

bool Batch::removeAllFilesInDir(const QString &path)
{
    qDebug() << "Removing all files from" << path;
    QDir dir(path);
    QStringList files = dir.entryList(QStringList() << "*", QDir::Files);
    foreach (QString file, files)
    {
        qDebug() << file;
        dir.remove(file);
    }

    return true;
}

bool Batch::copyBatchFilesTo(const QString &newPath)
{
    //
    QDir dirFrom(mPath);
    QDir dirTo(newPath);

    //
    for(int i = 0; i < mPages->size(); i++)
    {
        //
        Page *page = mPages->at(i);

        //page and thumb
        copyFile(dirFrom.absoluteFilePath(page->fileName()), dirTo.absoluteFilePath(page->fileName()));
        copyFile(dirFrom.absoluteFilePath(page->thumbName()), dirTo.absoluteFilePath(page->thumbName()));
    }

    return true;
}

bool Batch::copyFile(const QString &fromF, const QString &toF)
{
    QFile file(fromF);
    bool ok = file.copy(toF);

    qDebug() << "Copied file" << fromF << "to" << toF << (ok ? "OK" : "FAIL");

    return ok;
}

