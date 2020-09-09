## Potaufeu / FS dude / dede

### Description

potaufeu est un dédoublonneur de fichiers par ln.

Deux fichiers ayant même taille et même somme de contrôle sont regroupés en un seul inode.

Les droits et date du fichier ne sont pas prises en compte;
celui des deux qui sera effacé pour être lié sur l'autre perdra donc ses infos de date, par exemple (un rsync qui repassera dessus risque de vouloir le restaurer, à moins d'être appelé en -c).

### Noms

Les noms officiels du dédoublonneur sont:
* potaufeu
  En version longue "pote au F.E.U. (Féru d'Espace Utile)".
* dude
  * "dude" est la traduction anglaise de "pote"
  * Disk Usage Decrement
  * dé-du[pliquer] en verlan, car par défaut il traite ses paramètres en ordre inverse
* dede
  * plus rapide à taper que dude
  * un projet mien de dédoublonnage en base de données a aussi ce surnom

Son nom originel pas original du tout était lnertotal, mais on l'oubliera avec bonheur.
