/**
 * @file ssucli.h
 * @copyright 2012 Jolla Ltd.
 * @author Bernd Wachter <bernd.wachter@jollamobile.com>
 * @date 2012
 */

#ifndef _SsuCli_H
#define _SsuCli_H

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QDebug>

#include "libssu/ssu.h"
#include "ssuproxy.h"

class SsuCli: public QObject {
    Q_OBJECT

  public:
    SsuCli();
    ~SsuCli();

  public slots:
    void run();

  private:
    Ssu ssu;
    SsuProxy *ssuProxy;
    QSettings settings;
    int state;
    void usage(QString message="");
    void uidWarning(QString message="");
    void optDomain(QStringList opt);
    void optFlavour(QStringList opt);
    void optMode(QStringList opt);
    void optModel(QStringList opt);
    void optRegister(QStringList opt);
    void optRelease(QStringList opt);
    void optRepos(QStringList opt);
    void optStatus(QStringList opt);
    void optUpdateCredentials(QStringList opt);
    void optUpdateRepos(QStringList opt);

    enum Actions {
      Remove  = 0,
      Add     = 1,
      Disable = 2,
      Enable  = 3,
    };

    void optModifyRepo(enum Actions action, QStringList opt);

    void optAddRepo(QStringList opt) { optModifyRepo(Add, opt); }
    void optRemoveRepo(QStringList opt) { optModifyRepo(Remove, opt); }
    void optEnableRepo(QStringList opt) { optModifyRepo(Enable, opt); }
    void optDisableRepo(QStringList opt) { optModifyRepo(Disable, opt); }

    enum State {
      Idle,
      Busy,
      UserError
    };

  private slots:
    void handleResponse();
    void handleDBusResponse();

  signals:
    void done();

};

#endif
