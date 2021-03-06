#include "progressslideshowdialog.h"
#include "ui_progressslideshowdialog.h"
#include "util.h"
#include "mydebug.h"

#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QDesktopWidget>
#include <QDebug>
#include <QProcess>

/* Progress dialog with slideshow
 *
 * Initial author: Floris Bos
 * Maintained by Raspberry Pi
 *
 * See LICENSE.txt for license details
 *
 */

ProgressSlideshowDialog::ProgressSlideshowDialog(const QStringList &slidesDirectories, const QString &statusMsg, int changeInterval, const QString &drive, QWidget *parent, bool readmode) :
    QDialog(parent),
    _drive(drive),
    _pos(0),
    _changeInterval(changeInterval),
    _maxSectors(0),
    _pausedAt(0),
    _readmode(readmode),
    ui(new Ui::ProgressSlideshowDialog)
{
    ui->setupUi(this);
    setLabelText(statusMsg);

    ui->imagespace->setScaledContents(true); //Scale all slides to be the same size

    QRect s = QApplication::desktop()->screenGeometry();
    if (s.height() < 400)
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    else
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

    foreach (QString slidesDirectory, slidesDirectories)
    {
        QDir dir(slidesDirectory, "*.jpg *.jpeg *.png");
        if (dir.exists())
        {
            QStringList s = dir.entryList();
            s.sort();

            foreach (QString slide, s)
            {
                _slides.append(slidesDirectory+"/"+slide);
            }
        }
    }
    //qDebug() << "Available slides" << _slides;

    if (_slides.isEmpty())
    {
        /* Resize window to just show progress bar */
        ui->imagespace->setMinimumSize(0, 0);
        resize(this->width(), 50);

    }
    else
    {
        /* Resize window to size of largest image in slide directory */
        int maxwidth=0;
        int maxheight=0;
        foreach (QString slide, _slides)
        {   //Get largest slide dimension
            QPixmap pix(slide);
            maxwidth = qMax(maxwidth, pix.width());
            maxheight = qMax(maxheight, pix.height());
        }
        maxheight += ui->frame->height(); //89 for lower progress bar
        //Ensure it is smaller than physical screen
        maxwidth = qMin(maxwidth, s.width()-10);
        maxheight = qMin(maxheight, s.height()-100);
        //Resize dialog box. imagesize will exapnd to fit
        resize(maxwidth, maxheight);

        QPixmap pixmap(_slides.first());

        ui->imagespace->setPixmap(pixmap);

        connect(&_timer, SIGNAL(timeout()), this, SLOT(nextSlide()));
        _timer.start(changeInterval * 1000);
    }
    connect(&_iotimer, SIGNAL(timeout()), this, SLOT(updateIOstats()));
    enableIOaccounting();
}

ProgressSlideshowDialog::~ProgressSlideshowDialog()
{
    delete ui;
}

void ProgressSlideshowDialog::setLabelText(const QString &text)
{
    QString txt = text;
    txt.replace('\n',' ');
    ui->statusLabel->setText(txt);
    //qDebug() << text;
}

void ProgressSlideshowDialog::setMBWrittenText(const QString &text)
{
    QString txt = text;
    txt.replace('\n',' ');
    ui->mbwrittenLabel->setText(txt);
    //qDebug() << text;
}


void ProgressSlideshowDialog::nextSlide()
{
    if (++_pos >= _slides.size())
        _pos = 0;

    QString newSlide = _slides.at(_pos);
    if (QFile::exists(newSlide))
        ui->imagespace->setPixmap(QPixmap(newSlide));
}

/* IO accounting functionality for analyzing SD card write speed / showing progress */

void ProgressSlideshowDialog::enableIOaccounting()
{
    _sectorsStart = sectorsAccessed();
    _t1.start();
    _iotimer.start(1000);
    QProcess::execute("rm /tmp/progress");
}

void ProgressSlideshowDialog::disableIOaccounting()
{
    _iotimer.stop();
    ui->mbwrittenLabel->setText("");
}

void ProgressSlideshowDialog::captureIOaccounting(uint *paused)
{
    TRACE
    if (paused)
    {
        *paused = sectorsAccessed()-_sectorsStart;
    }
    DBG (QString::number(*paused));
}

void ProgressSlideshowDialog::restoreIOaccounting(uint paused)
{
    TRACE
    DBG (QString::number(paused));
    _iotimer.stop();
    if (paused)
    {
        _sectorsStart = sectorsAccessed()-paused;
        updateIOstats();
    }
    _iotimer.start(1000);
}

void ProgressSlideshowDialog::pauseIOaccounting()
{
    _iotimer.stop();
    _pausedAt = sectorsAccessed();

}

void ProgressSlideshowDialog::resumeIOaccounting()
{
    _sectorsStart += sectorsAccessed()-_pausedAt;
    _iotimer.start(1000);
}

void ProgressSlideshowDialog::changeDrive(const QString &drive)
{
    pauseIOaccounting();
    _drive = drive;
    resumeIOaccounting();
}


void ProgressSlideshowDialog::setMaximum(qint64 bytes)
{
    /* restrict to size of 1TB since the progressbar expects an int32 */
    /* to prevent overflow */
    if (bytes > 1099511627775LL) /* == 2147483648 * 512 -1*/
        bytes = 1099511627775LL;
    _maxSectors = bytes/512;
    ui->progressBar->setMaximum(_maxSectors);
}

void ProgressSlideshowDialog::updateIOstats()
{
    static int last_percent=-1;
    uint sectors = sectorsAccessed()-_sectorsStart;

    double sectorsPerSec = sectors * 1000.0 / _t1.elapsed();
    if (_maxSectors)
    {
        sectors = qMin(_maxSectors, sectors);
        ui->progressBar->setValue(sectors);

        double secondsleft = ((double)_maxSectors - (double)sectors)/sectorsPerSec;
        uint remaining = (uint) secondsleft;
        uint secs = remaining %60;
        remaining /=60; //mins
        uint mins = remaining %60;
        remaining /=60; //hours
        uint hrs = remaining ;

        QString mode;
        if (_readmode)
            mode=tr("%1 MB of %2 MB read (%3 MB/sec) Remaining: %4:%5:%6");
        else
            mode=tr("%1 MB of %2 MB written (%3 MB/sec) Remaining: %4:%5:%6");

        setMBWrittenText(mode
            .arg(QString::number(sectors/2048))
            .arg(QString::number(_maxSectors/2048))
            .arg(QString::number(sectorsPerSec/2048.0, 'f', 1))
            .arg(hrs,2,10,QLatin1Char( '0' ))
            .arg(mins,2,10,QLatin1Char( '0' ))
            .arg(secs,2,10,QLatin1Char( '0' )));

        int percent = (100*sectors)/_maxSectors;
        if (last_percent != percent)
        {
            last_percent=percent;
            QString progress = QString("%1 %\n").arg(QString::number(percent));
            QByteArray output= progress.toUtf8();
            QFile f("/tmp/progress");
            f.open(f.Append);
            f.write(output);
            f.close();

        }
    }
    else
    {
        QString mode;
        if (_readmode)
            mode=tr("%1 MB read (%2 MB/sec)");
        else
            mode=tr("%1 MB written (%2 MB/sec)");

        setMBWrittenText(mode
            .arg(QString::number(sectors/2048), QString::number(sectorsPerSec/2048.0, 'f', 1)));

        int percent = sectors/2048;
        if (last_percent != percent)
        {
            last_percent=percent;
            QString progress = QString("%1 MB\n").arg(QString::number(percent));
            QByteArray output= progress.toUtf8();
            QFile f("/tmp/progress");
            f.open(f.Append);
            f.write(output);
            f.close();
        }
    }
}

void ProgressSlideshowDialog::updateProgress(qint64 value)
{
    int fraction = (int)(value>>9);
    ui->progressBar->setValue(fraction);
    //qDebug() << "updateProgress " << fraction;
}

uint ProgressSlideshowDialog::sectorsAccessed()
{
    /* Poll kernel counters to get number of bytes written
     *
     * Fields available in /sys/block/<DEVICE>/stat
     * (taken from https://www.kernel.org/doc/Documentation/block/stat.txt )
     *
     * Name            units         description
     * ----            -----         -----------
     * read I/Os       requests      number of read I/Os processed
     * read merges     requests      number of read I/Os merged with in-queue I/O
     * read sectors    sectors       number of sectors read
     * read ticks      milliseconds  total wait time for read requests
     * write I/Os      requests      number of write I/Os processed
     * write merges    requests      number of write I/Os merged with in-queue I/O
     * write sectors   sectors       number of sectors written
     * write ticks     milliseconds  total wait time for write requests
     * in_flight       requests      number of I/Os currently in flight
     * io_ticks        milliseconds  total time this block device has been active
     * time_in_queue   milliseconds  total wait time for all requests
     */

    int field=6;
    if (_readmode)
        field=2;

    uint numsectors=0;

    QFile f(sysclassblock(_drive)+"/stat");
    f.open(f.ReadOnly);
    QByteArray ioline = f.readAll().simplified();
    f.close();

    QList<QByteArray> stats = ioline.split(' ');

    if (stats.count() >= field)
        numsectors = stats.at(field).toUInt(); /* Read or write sectors */

    if (numsectors > 2147483647)        //Maybe use MAX_INT from limits.h?
       numsectors = 2147483647;
    return numsectors;
}
