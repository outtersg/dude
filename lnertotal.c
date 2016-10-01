#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/mman.h>

/* Cet utilitaire est destiné à tourner sur un Mac OS X 10.8; les optimisations du style linkat, fdopendir, sont donc remises à plus tard. */

int g_realiser;

/*- Basiques -----------------------------------------------------------------*/

void err(char * quoi, ...)
{
	va_list args;
	va_start(args, quoi);
	fprintf(stderr, "# ");
	vfprintf(stderr, quoi, args);
	fprintf(stderr, "\n");
	va_end(args);
}

char B64[64];

void initB64()
{
	/* Inspiré du code du base64. */
	
	int i;
	char c;

	i = -1;
	for(c = 'A'; c <= 'Z'; ++c)
		B64[++i] = c;
	for(c = 'a'; c <= 'z'; ++c)
		B64[++i] = c;
	for(c = '0'; c <= '9'; ++c)
		B64[++i] = c;
	/* Petite entorse au base64, pour du nom de fichier. */
	B64[++i] = '_';
	B64[++i] = '-';
}

void mktemp6(char * ptr)
{
	int val = rand();
	int n;
	int c;
	
	for(n = 6; --n >= 0; ++ptr, val >>= 6)
	{
		c = val & 0x03F;
		*ptr = B64[c];
	}
}

#define TROUVER(trouve, pos, quoi, Type, champ, entrees, nEntrees) \
	do \
	{ \
		int _d = 0; \
		int _f = nEntrees; \
		while(_f > _d) \
			if(entrees[pos = (_d + _f) / 2].champ == quoi) \
				break; \
			else if(entrees[pos].champ < quoi) \
				_d = pos + 1; \
			else \
				_f = pos; \
		if(!(trouve = (_f > _d))) \
			pos = _d; \
	} \
	while(0)

#define PLACERN(Type, entrees, nEntrees, nEntreesAllouees, pos, tailleAlloc) \
	do \
	{ \
		if(nEntrees >= nEntreesAllouees) \
		{ \
			nEntreesAllouees += nEntreesAllouees > tailleAlloc * 16 ? nEntreesAllouees / 16 : tailleAlloc; \
			Type * entrees2 = (Type *)malloc(nEntreesAllouees * sizeof(Type)); \
			if(entrees) \
			{ \
			memcpy(entrees2, entrees, pos * sizeof(Type)); \
			free(entrees); \
			} \
			entrees = entrees2; \
		} \
		if(nEntrees > pos) \
			memcpy(&entrees[pos + 1], &entrees[pos], (nEntrees - pos) * sizeof(Type)); \
		++nEntrees; \
	} \
	while(0);

#define PLACER(Type, entrees, nEntrees, nEntreesAllouees, pos) PLACERN(Type, entrees, nEntrees, nEntreesAllouees, pos, 64)

#define TROUVEROUCREER(trouve, pos, quoi, Type, champ, entrees, nEntrees, nEntreesAllouees, init) \
	do \
	{ \
		TROUVER(trouve, pos, quoi, Type, champ, entrees, nEntrees); \
		if(!trouve) \
		{ \
			PLACER(Type, entrees, nEntrees, nEntreesAllouees, pos); \
			entrees[pos].champ = quoi; \
			init; \
		} \
	} \
	while(0);

/*- Somme --------------------------------------------------------------------*/

#include "crc32.c"
typedef uint32_t crc_t;

void crcFichier(int fd, size_t taille, crc_t * ptrCrc)
{
	void * mem = taille ? mmap(NULL, taille, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
	if(mem == MAP_FAILED)
	{
		err("mmap a échoué: %s", strerror(errno));
		exit(1);
	}
	#if 0
	*ptrCrc = 0; /* À FAIRE: n'y a-t-il pas une valeur de départ pour les crc32? */
	*ptrCrc = calculate_crc32c(*ptrCrc, mem, taille);
	#else
	crc32mem(mem, taille, ptrCrc);
	#endif
	if(mem)
		munmap(mem, taille);
}

/*- Chemin -------------------------------------------------------------------*/

typedef struct Chemin
{
	char * c;
	struct Chemin * p;
} Chemin;

Chemin * CheminNouveau(Chemin * pere, const char * chemin)
{
	Chemin * n = (Chemin *)malloc(sizeof(Chemin));
	n->p = pere;
	n->c = (char *)malloc(strlen(chemin) + 1);
	strcpy(n->c, chemin);
	return n;
}

static char tempCheminComplet[MAXPATHLEN];

char * CheminComplet(Chemin * chemin, char * chaineChemin)
{
	Chemin * ptr;
	int tailleChemin;
	int tailleSegment;
	
	if(!chaineChemin) chaineChemin = tempCheminComplet;
	
	for(ptr = chemin, tailleChemin = -1; ptr; ptr = ptr->p)
		tailleChemin += 1 + strlen(ptr->c);
	chaineChemin[tailleChemin] = 0;
	for(ptr = chemin; ptr;)
	{
		tailleChemin -= (tailleSegment = strlen(ptr->c));
		memcpy(&chaineChemin[tailleChemin], ptr->c, tailleSegment);
		if((ptr = ptr->p))
			chaineChemin[--tailleChemin] = '/';
	}
	return chaineChemin;
}

int CheminRaccrocher(Chemin * chemin, Chemin * dossierFichierARaccrocher, char * cheminARaccrocher)
{
	char chaineChemin[MAXPATHLEN];
	char raccrocheTemp[MAXPATHLEN];
	int tailleRaccroche;
	
	CheminComplet(chemin, chaineChemin);
	
	fprintf(stdout, "%s <- %s/%s\n", chaineChemin, CheminComplet(dossierFichierARaccrocher, NULL), cheminARaccrocher);
	
	if(g_realiser)
	{
		strcpy(raccrocheTemp, cheminARaccrocher);
		tailleRaccroche = strlen(cheminARaccrocher);
		raccrocheTemp[tailleRaccroche] = '.';
		raccrocheTemp[tailleRaccroche + 7] = 0;
		mktemp6(&raccrocheTemp[tailleRaccroche + 1]);
		if(link(chaineChemin, raccrocheTemp) != 0)
		{
			err("link(%s, %s): %s", chaineChemin, raccrocheTemp, strerror(errno));
			return -1;
		}
		if(rename(raccrocheTemp, cheminARaccrocher) != 0)
		{
			err("rename(%s, %s): %s", raccrocheTemp, cheminARaccrocher, strerror(errno));
			unlink(raccrocheTemp);
			return -1;
		}
	}
	return 0;
}

int crcChemin(Chemin * chemin, int taille, crc_t * ptrCrc)
{
	int f;
	
	f = open(CheminComplet(chemin, NULL), O_RDONLY);
	if(f < 0) { err("Ouverture de %s impossible: %s", CheminComplet(chemin, NULL), strerror(errno)); return 0; }
	crcFichier(f, taille, ptrCrc);
	close(f);
	
	return 1;
}

/*- Inodes -------------------------------------------------------------------*/

typedef struct CorrInode
{
	ino_t inode;
	Chemin * chemin;
} CorrInode;

/*- Tailles ------------------------------------------------------------------*/

#define D_CRC_CALCULE 0x01

typedef struct InfosFichier
{
	Chemin * chemin;
	crc_t crc;
	uint32_t drapeaux;
} InfosFichier;

void InfosFichierInit(InfosFichier * infos, Chemin * chemin)
{
	infos->chemin = chemin;
	infos->drapeaux = 0;
}

typedef struct Taillis
{
	size_t taille;
	InfosFichier * fichiers;
	int nFichiers;
	int nFichiersAlloues;
} Taillis;

void TaillisInitHorsCle(Taillis * taillis)
{
	taillis->fichiers = NULL;
	taillis->nFichiers = 0;
	taillis->nFichiersAlloues = 0;
}

Chemin * TaillisTrouverOuCreer(Taillis * taillis, Chemin * cheminDossier, struct dirent * fichier, struct stat * infos, int fd)
{
	InfosFichier infosFichier;
	infosFichier.drapeaux = 0;
	int pos;
	
	for(pos = -1; ++pos < taillis->nFichiers;)
	{
		/* On ne calcule le crc qu'ici, si un autre fichier de la même taille existe déjà. Inutile de le calculer si le fichier est tout seul (sans passage dans la boucle). */
		
		if(pos == 0)
		{
			crcFichier(fd, infos->st_size, &infosFichier.crc);
			infosFichier.drapeaux |= D_CRC_CALCULE;
		}
		
		/* Notre comparé a-t-il déjà été calculé? */
		
		if(!(taillis->fichiers[pos].drapeaux & D_CRC_CALCULE))
		{
			crcChemin(taillis->fichiers[pos].chemin, infos->st_size, &taillis->fichiers[pos].crc);
			taillis->fichiers[pos].drapeaux |= D_CRC_CALCULE;
		}
		
		if(!memcmp(&infosFichier.crc, &taillis->fichiers[pos].crc, sizeof(crc_t)))
			return taillis->fichiers[pos].chemin;
	}
	
	/* On crée, nous sommes le premier fichier de cette taille avec cette somme, nous devenons donc référence pour cette somme. */
	
	PLACERN(InfosFichier, taillis->fichiers, taillis->nFichiers, taillis->nFichiersAlloues, pos, 4);
	infosFichier.chemin = CheminNouveau(cheminDossier, fichier->d_name);
	memcpy(&taillis->fichiers[pos], &infosFichier, sizeof(InfosFichier));
	
	return NULL;
}

/*- Racine -------------------------------------------------------------------*/

typedef struct Racine
{
	Chemin * cheminActuel;
	CorrInode * inodes;
	int nInodes;
	int nInodesAlloues;
	
	Taillis * taillis;
	int nTaillis;
	int nTaillisAlloues;
} Racine;

void RacineInit(Racine * racine, const char * chemin)
{
	racine->cheminActuel = CheminNouveau(NULL, chemin);
	
	racine->inodes = NULL;
	racine->nInodes = 0;
	racine->nInodesAlloues = 0;
	
	racine->taillis = NULL;
	racine->nTaillis = 0;
	racine->nTaillisAlloues = 0;
}

Chemin * RacineIntegrerFichierATaillis(Racine * racine, struct dirent * f)
{
	Chemin * cheminRaccrochage = NULL;
	struct stat infos;
	size_t taille;
	
	int fd = open(f->d_name, O_RDONLY);
	if(fstat(fd, & infos)) { err("Impossible d'interroger %s/%s: %s", CheminComplet(racine->cheminActuel, NULL), f->d_name, strerror(errno)); goto e0; }
	taille = infos.st_size;
	
	/* Recherche par taille. */
	
	int posTaillis;
	int trouveTaillis;
	Taillis * taillis;
	TROUVEROUCREER(trouveTaillis, posTaillis, taille, Taillis, taille, racine->taillis, racine->nTaillis, racine->nTaillisAlloues, TaillisInitHorsCle(& racine->taillis[posTaillis]));
	taillis = &racine->taillis[posTaillis];
	
	/* Pour chacun des fichiers du taillis, on va essayer de voir si on ne peut s'y raccrocher. */
	
	cheminRaccrochage = TaillisTrouverOuCreer(taillis, racine->cheminActuel, f, & infos, fd);

e0:
	close(fd);
	
	return cheminRaccrochage;
}

Chemin * RacineRaccrochage(struct Racine * racine, struct dirent * f)
{
	Chemin * raccrochage;
	int pos;
	int trouve;
	TROUVEROUCREER(trouve, pos, f->d_fileno, CorrInode, inode, racine->inodes, racine->nInodes, racine->nInodesAlloues, );
	
	if(!trouve)
		/* Si pas trouvé dans le cache par inode, c'est la première fois que l'on tombe sur cet inode. On recherche son Chemin de référence. */
		racine->inodes[pos].chemin = RacineIntegrerFichierATaillis(racine, f);
	
	return racine->inodes[pos].chemin;
}

/*- Boulot -------------------------------------------------------------------*/

void analyser(Racine * racine, struct dirent * f)
{
	Chemin * chemin;
	if((chemin = RacineRaccrochage(racine, f)))
		CheminRaccrocher(chemin, racine->cheminActuel, f->d_name);
}


void parcourir(Racine * racine, char * nom)
{
	Chemin * cheminActuel = racine->cheminActuel;
	
	int tailleNom = strlen(nom);
	Chemin * monChemin;
	if(tailleNom > 0 && nom[0] == '/')
	{
		Chemin * monChemin = CheminNouveau(NULL, nom);
		racine->cheminActuel = monChemin;
	}
	else
	{
		Chemin * monChemin = CheminNouveau(racine->cheminActuel, nom);
		racine->cheminActuel = monChemin;
	}
	
	DIR * dossier;
	struct dirent * entree;
	
	dossier = opendir(nom);
	if(!dossier)
	{
		err("Le dossier %s a disparu au moment d'y accéder", nom);
		goto e0;
	}
	fchdir(dirfd(dossier));
	while((entree = readdir(dossier)))
	{
		if(0 == strcmp(".", entree->d_name) || 0 == strcmp("..", entree->d_name))
			continue;
		if(entree->d_type == DT_DIR)
		{
			parcourir(racine, entree->d_name);
			fchdir(dirfd(dossier));
		}
		else if(entree->d_type == DT_REG)
			analyser(racine, entree);
	}
	closedir(dossier);
	
e0:
	racine->cheminActuel = cheminActuel;
}

int main(int argc, char ** argv)
{
	/* On pousse par défaut en sens inverse. Le dernier argument est considéré comme la référence (à analyser en premier, donc), afin que sur du 2016-*, le dernier en date soit considéré comme la référence. */
	
	int sens = -1;
	int i;
	int nFichiers;
	char ** fichiers = (char **)malloc(argc * sizeof(char *)); /* Mieux vaut réserver trop que pas assez. */
	Racine racine;
	RacineInit(& racine, getcwd(NULL, MAXPATHLEN));
	DIR * ici = opendir(".");
	int fdIci = dirfd(ici);
	
	g_realiser = 1;
	for(i = 0; ++i < argc;)
		if(0 == strcmp(argv[i], "-r"))
			sens = -sens;
		else if(0 == strcmp(argv[i], "-n"))
			g_realiser = 0;
		else if(sens > 0)
			fichiers[nFichiers++] = argv[i];
		else
		{
			memmove(&fichiers[1], fichiers, (argc - 1) * sizeof(char *));
			fichiers[0] = argv[i];
			++nFichiers;
		}
	
	initB64();
	srand(time(NULL));
	
	for(i = 0; i < nFichiers; ++i)
	{
		fchdir(fdIci);
		parcourir(& racine, fichiers[i]);
	}
	
	closedir(ici);
}
