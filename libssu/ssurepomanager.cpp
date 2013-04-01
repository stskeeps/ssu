/**
 * @file ssurepomanager.cpp
 * @copyright 2013 Jolla Ltd.
 * @author Bernd Wachter <bwachter@lart.info>
 * @date 2013
 */

#include <QStringList>
#include <QRegExp>
#include <QDirIterator>

#include "ssudeviceinfo.h"
#include "ssurepomanager.h"
#include "ssucoreconfig.h"
#include "ssulog.h"
#include "ssuvariables.h"
#include "ssu.h"

#include "../constants.h"

SsuRepoManager::SsuRepoManager(): QObject() {

}

void SsuRepoManager::add(QString repo, QString repoUrl){
  SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();

  if (repoUrl == ""){
    // just enable a repository which has URL in repos.ini
    QStringList enabledRepos;
    if (ssuSettings->contains("enabled-repos"))
      enabledRepos = ssuSettings->value("enabled-repos").toStringList();

    enabledRepos.append(repo);
    enabledRepos.removeDuplicates();
    ssuSettings->setValue("enabled-repos", enabledRepos);
  } else
    ssuSettings->setValue("repository-urls/" + repo, repoUrl);

  ssuSettings->sync();
}

void SsuRepoManager::disable(QString repo){
  SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
  QStringList disabledRepos;

  if (ssuSettings->contains("disabled-repos"))
    disabledRepos = ssuSettings->value("disabled-repos").toStringList();

  disabledRepos.append(repo);
  disabledRepos.removeDuplicates();

  ssuSettings->setValue("disabled-repos", disabledRepos);
  ssuSettings->sync();
}

void SsuRepoManager::enable(QString repo){
  SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
  QStringList disabledRepos;

  if (ssuSettings->contains("disabled-repos"))
    disabledRepos = ssuSettings->value("disabled-repos").toStringList();

  disabledRepos.removeAll(repo);
  disabledRepos.removeDuplicates();

  ssuSettings->setValue("disabled-repos", disabledRepos);
  ssuSettings->sync();
}

void SsuRepoManager::remove(QString repo){
  SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
  if (ssuSettings->contains("repository-urls/" + repo))
    ssuSettings->remove("repository-urls/" + repo);

  if (ssuSettings->contains("enabled-repos")){
    QStringList enabledRepos = ssuSettings->value("enabled-repos").toStringList();
    if (enabledRepos.contains(repo)){
      enabledRepos.removeAll(repo);
      enabledRepos.removeDuplicates();
      ssuSettings->setValue("enabled-repos", enabledRepos);
    }
  }

  ssuSettings->sync();
}

void SsuRepoManager::update(){
  // - delete all non-ssu managed repositories (missing ssu_ prefix)
  // - create list of ssu-repositories for current adaptation
  // - go through ssu_* repositories, delete all which are not in the list; write others

  SsuDeviceInfo deviceInfo;
  QStringList ssuFilters;

  SsuCoreConfig *ssuSettings = SsuCoreConfig::instance();
  int deviceMode = ssuSettings->value("deviceMode").toInt();

  SsuLog *ssuLog = SsuLog::instance();

  // if device is misconfigured, always assume release mode
  bool rndMode = false;

  if ((deviceMode & Ssu::DisableRepoManager) == Ssu::DisableRepoManager){
    ssuLog->print(LOG_INFO, "Repo management requested, but not enabled (option 'deviceMode')");
    return;
  }

  if ((deviceMode & Ssu::RndMode) == Ssu::RndMode)
    rndMode = true;

  // get list of device-specific repositories...
  QStringList repos = deviceInfo.repos(rndMode);

  // strict mode enabled -> delete all repositories not prefixed by ssu
  // assume configuration error if there are no device repos, and don't delete
  // anything, even in strict mode
  if ((deviceMode & Ssu::LenientMode) != Ssu::LenientMode && !repos.isEmpty()){
    QDirIterator it(ZYPP_REPO_PATH, QDir::AllEntries|QDir::NoDot|QDir::NoDotDot);
    while (it.hasNext()){
      it.next();
      if (it.fileName().left(4) != "ssu_"){
        ssuLog->print(LOG_INFO, "Strict mode enabled, removing unmanaged repository " + it.fileName());
        QFile(it.filePath()).remove();
      }
    }
  }

  // ... delete all ssu-managed repositories not valid for this device ...
  ssuFilters.append("ssu_*");
  QDirIterator it(ZYPP_REPO_PATH, ssuFilters);
  while (it.hasNext()){
    QString f = it.next();

    QStringList parts = it.fileName().split("_");
    // repo file structure is ssu_<reponame>_<rnd|release>.repo -> splits to 3 parts
    if (parts.count() == 3){
      if (!repos.contains(parts.at(1)) ||
          parts.at(2) != (rndMode ? "rnd.repo" : "release.repo" ))
        QFile(it.filePath()).remove();
    } else
      QFile(it.filePath()).remove();
  }

  // ... and create all repositories required for this device
  foreach (const QString &repo, repos){
    QFile repoFile(QString("%1/ssu_%2_%3.repo")
                   .arg(ZYPP_REPO_PATH)
                   .arg(repo)
                   .arg(rndMode ? "rnd" : "release"));

    if (repoFile.open(QIODevice::WriteOnly | QIODevice::Text)){
      QTextStream out(&repoFile);
      // TODO, add -rnd or -release if we want to support having rnd and
      //       release enabled at the same time
      out << "[" << repo << "]" << endl
          << "name=" << repo << endl
          << "failovermethod=priority" << endl
          << "type=rpm-md" << endl
          << "gpgcheck=0" << endl
          << "enabled=1" << endl;

      if (rndMode)
        out << "baseurl=plugin:ssu?rnd&repo=" << repo << endl;
      else
        out << "baseurl=plugin:ssu?repo=" << repo << endl;

      out.flush();
    }
  }
}

// RND repos have flavour (devel, testing, release), and release (latest, next)
// Release repos only have release (latest, next, version number)
QString SsuRepoManager::url(QString repoName, bool rndRepo,
                            QHash<QString, QString> repoParameters,
                            QHash<QString, QString> parametersOverride){
  QString r;
  QStringList configSections;
  SsuVariables var;
  SsuLog *ssuLog = SsuLog::instance();
  SsuCoreConfig *settings = SsuCoreConfig::instance();
  QSettings *repoSettings = new QSettings(SSU_REPO_CONFIGURATION, QSettings::IniFormat);
  SsuDeviceInfo deviceInfo;

  //errorFlag = false;

  settings->sync();

  // fill in all arbitrary variables from ssu.inie
  var.resolveSection(settings, "repository-url-variables", &repoParameters);

  // add/overwrite some of the variables with sane ones
  if (rndRepo){
    repoParameters.insert("flavour",
                          repoSettings->value(
                            settings->flavour()+"-flavour/flavour-pattern").toString());
    repoParameters.insert("flavourPattern",
                          repoSettings->value(
                            settings->flavour()+"-flavour/flavour-pattern").toString());
    repoParameters.insert("flavourName", settings->flavour());
    configSections << settings->flavour()+"-flavour" << "rnd" << "all";

    // Make it possible to give any values with the flavour as well.
    // These values can be overridden later with domain if needed.
    var.resolveSection(repoSettings, settings->flavour()+"-flavour", &repoParameters);
  } else {
    configSections << "release" << "all";
  }

  repoParameters.insert("release", settings->release(rndRepo));

  if (!repoParameters.contains("debugSplit"))
    repoParameters.insert("debugSplit", "packages");

  if (!repoParameters.contains("arch"))
    repoParameters.insert("arch", settings->value("arch").toString());

  // Override device model (and therefore all the family, ... stuff)
  if (parametersOverride.contains("model"))
    deviceInfo.setDeviceModel(parametersOverride.value("model"));

  // read adaptation from settings, in case it can't be determined from
  // board mappings. this is obsolete, and will be dropped soon
  if (settings->contains("adaptation"))
    repoParameters.insert("adaptation", settings->value("adaptation").toString());

  repoParameters.insert("deviceFamily", deviceInfo.deviceFamily());
  repoParameters.insert("deviceModel", deviceInfo.deviceModel());

  // Those keys have now been obsoleted by generic variables, support for
  // it will be removed soon
  QStringList keys;
  keys << "chip" << "adaptation" << "vendor";
  foreach(QString key, keys){
    QString value;
    if (deviceInfo.getValue(key,value))
      repoParameters.insert(key, value);
  }

  repoName = deviceInfo.adaptationVariables(repoName, &repoParameters);

  // Domain variables
  // first read all variables from default-domain
  var.resolveSection(repoSettings, "default-domain", &repoParameters);

  // then overwrite with domain specific things if that block is available
  var.resolveSection(repoSettings, settings->domain()+"-domain", &repoParameters);

  // override arbitrary variables, mostly useful for generating mic URLs
  QHash<QString, QString>::const_iterator i = parametersOverride.constBegin();
  while (i != parametersOverride.constEnd()){
    repoParameters.insert(i.key(), i.value());
    i++;
  }

  if (settings->contains("repository-urls/" + repoName))
    r = settings->value("repository-urls/" + repoName).toString();
  else {
    foreach (const QString &section, configSections){
      repoSettings->beginGroup(section);
      if (repoSettings->contains(repoName)){
        r = repoSettings->value(repoName).toString();
        repoSettings->endGroup();
        break;
      }
      repoSettings->endGroup();
    }
  }

  return var.resolveString(r, &repoParameters);
}