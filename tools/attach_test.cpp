// Container-only: print the attachment assembly a body mesh resolves to (AssetStore::attachmentsForModel).
#include "store/AssetStore.h"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char**argv){ QCoreApplication a(argc,argv);
  if(argc<3){fprintf(stderr,"usage: attach_test <bundles> <body.smd>\n");return 2;}
  AssetStore s; QString e; if(!s.open(QString::fromLocal8Bit(argv[1]),&e)){fprintf(stderr,"open:%s\n",qPrintable(e));return 1;}
  auto asm_=s.attachmentsForModel(QString::fromLocal8Bit(argv[2]).toLower());
  printf("bodyAo: %s\n", qPrintable(asm_.bodyAo.isEmpty()?QString("(none found)"):asm_.bodyAo));
  printf("pieces: %d\n", asm_.pieces.size());
  for(const auto&p:asm_.pieces) printf("  [%s] bone='%s' mesh=%s  ao=%s\n", qPrintable(p.label), qPrintable(p.bone), qPrintable(p.smdPath.isEmpty()?QString("(unresolved)"):p.smdPath), qPrintable(p.aoPath));
  return 0; }
