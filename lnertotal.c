/*
 * Copyright (c) 2016-2020 Guillaume Outters
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
#include <sys/ioctl.h>
#include <sys/ttycom.h>

/* Cet utilitaire est destiné à tourner sur un Mac OS X 10.8; les optimisations du style linkat, fdopendir, sont donc remises à plus tard. */

int g_realiser;
int g_symbolique;
long g_tailleMin;

/*- Basiques -----------------------------------------------------------------*/

void err(char * quoi, ...)
{
	va_list args;
	va_start(args, quoi);
	/* On prélève un échappement ANSI si présent; sinon on rougit pour faire ressortir l'erreur. */
	char * q = quoi;
	if(quoi[0] == '' && quoi[1] == '[')
	{
		while(*++q && *q != 'm') {}
		if(*q == 'm')
			++q;
		else
			q = quoi;
	}
	if(q > quoi)
	{
		fwrite(quoi, sizeof(char), q - quoi, stderr);
		quoi = q;
	}
	else
		fprintf(stderr, "[31m");
	/* Ponte du message. */
	fprintf(stderr, "# ");
	vfprintf(stderr, quoi, args);
	fprintf(stderr, "[0m\n");
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
				memcpy(&entrees2[pos + 1], &entrees[pos], (nEntrees - pos) * sizeof(Type)); \
			free(entrees); \
			} \
			entrees = entrees2; \
		} \
		else if(nEntrees > pos) \
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

#undef BUFSIZ
#define BUFSIZ 1048576
#include "crc32.c"
typedef uint32_t crc_t;

void initSomme()
{
}

int crcFichier(int fd, size_t taille, crc_t * ptrCrc)
{
	void * mem = taille ? mmap(NULL, taille, PROT_READ, MAP_PRIVATE, fd, 0) : NULL;
	if(mem == MAP_FAILED)
	{
		err("[33m(mmap a échoué: %s, tentative par lecture de fichier)", strerror(errno));
		off_t pos;
		if(lseek(fd, 0, SEEK_SET) < 0) { err("lseek a échoué: %s", strerror(errno)); return -1; }
		if(crc32(fd, ptrCrc, &pos) != 0) { err("crc32 a échoué: %s", strerror(errno)); return -1; }
		return 0;
	}
	#if 0
	*ptrCrc = 0; /* À FAIRE: n'y a-t-il pas une valeur de départ pour les crc32? */
	*ptrCrc = calculate_crc32c(*ptrCrc, mem, taille);
	#else
	/* À FAIRE: appeler avancee durant les opérations longues (ex.: dans un CRC, si l'on a détecté en l'attaquant que l'on allait travailler sur un "gros" fichier). */
	crc32mem(mem, taille, ptrCrc);
	#endif
	if(mem)
		munmap(mem, taille);
	
	return 0;
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

int lier(const char * pilier, const char * raccroche)
{
	if(!g_symbolique)
	{
		if(link(pilier, raccroche) != 0)
		{
			err("link(%s, %s): %s", pilier, raccroche, strerror(errno));
			return -1;
		}
	}
	else
	{
		if(symlink(pilier, raccroche) != 0)
		{
			err("symlink(%s, %s): %s", pilier, raccroche, strerror(errno));
			return -1;
		}
	}
	
	return 0;
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
		if(lier(chaineChemin, raccrocheTemp) != 0)
			return -1;
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
	const char * cChemin = CheminComplet(chemin, NULL);
	
	f = open(cChemin, O_RDONLY);
	if(f < 0) { err("Ouverture de %s impossible: %s", cChemin, strerror(errno)); return 0; }
	if(crcFichier(f, taille, ptrCrc) < 0)
		fprintf(stderr, "# %s: calcul du CRC impossible\n", cChemin);
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
			if(crcFichier(fd, infos->st_size, &infosFichier.crc) < 0)
			{
				fprintf(stderr, "# %s: calcul du CRC impossible\n", fichier->d_name);
				continue;
			}
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
	if(g_tailleMin >= 0 && taille < g_tailleMin) goto e0;
	
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

time_t g_temps;
int g_avancee_taille;
char * g_avancee_tampon;
int g_avancee_horloge_position;
const char * g_avancee_horloge = "\\-/|";

void initAvancee()
{
	g_temps = time(NULL);
	struct winsize taille;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &taille); /* À FAIRE: actualiser lorsque la fenêtre change de taille, avec g_avancee_tampon. */
	g_avancee_taille = taille.ws_col - 3; /* Un caractère horloge à gauche suivi d'un espace, et un espace à droite. */
	if(g_avancee_taille <= 0)
		g_avancee_taille = 125;
	g_avancee_tampon = (char *)malloc(g_avancee_taille + 2); /* Un caractère nul après le remplissage, un caractère nul après le chemin. */
	g_avancee_horloge_position = 4;
}

void avancee(Racine * racine, char * nomFichier)
{
	time_t t1 = time(NULL);
	if(t1 <= g_temps) return; /* À FAIRE: et si l'on ne vient pas de sortir une ligne complète. */
	g_temps = t1;
	
	Chemin * chemin = racine->cheminActuel;
	char * ptr;
	char * ptrChemin;
	int t;
	
	ptr = &g_avancee_tampon[g_avancee_taille + 2];
	*--ptr = 0;
	while(1)
	{
		if((ptr -= (t = strlen(nomFichier))) < &g_avancee_tampon[5])
		{
			ptr += t - 3;
			bcopy("...", ptr, 3);
			break;
		}
		else
			bcopy(nomFichier, ptr, t);
		
		if(!chemin)
			break;
		nomFichier = chemin->c;
		chemin = chemin->p;
		*--ptr = '/';
	}
	ptrChemin = ptr;
	*--ptr = 0;
	
	while(--ptr > g_avancee_tampon)
		*ptr = ' ';
	
	if(--g_avancee_horloge_position < 0) g_avancee_horloge_position = 3;
	fprintf(stdout, "%c %s%s\r", g_avancee_horloge[g_avancee_horloge_position], ptrChemin, ++ptr);
	fflush(stdout);
}

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
		avancee(racine, entree->d_name);
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
	int i, j;
	int nFichiers = 0;
	char ** fichiers = (char **)malloc(argc * sizeof(char *)); /* Mieux vaut réserver trop que pas assez. */
	Racine racine;
	RacineInit(& racine, getcwd(NULL, MAXPATHLEN));
	DIR * ici = opendir(".");
	int fdIci = dirfd(ici);
	
	g_realiser = 1;
	g_symbolique = 0;
	g_tailleMin = -1;
	for(i = 0; ++i < argc;)
		if(0 == strcmp(argv[i], "-r"))
			sens = -sens;
		else if(0 == strcmp(argv[i], "-n"))
			g_realiser = 0;
		else if(0 == strcmp(argv[i], "-s"))
			g_symbolique = 1;
		else if(0 == strcmp(argv[i], "-size"))
		{
			if(argv[i + 1] && argv[i + 1][0] == '+')
			{
				for(j = 0; argv[i + 1][++j];)
					if(argv[i + 1][j] < '0' || argv[i + 1][j] > '9')
						break;
				if(!argv[i + 1][j])
				{
					g_tailleMin = atol(&argv[i + 1][i]);
					++i;
					continue;
				}
			}
			err("# L'option -size prend pour argument un +<taille min>, par exemple: -size +1");
			exit(1);
		}
		else if(sens > 0)
			fichiers[nFichiers++] = argv[i];
		else
		{
			memmove(&fichiers[1], fichiers, nFichiers * sizeof(char *));
			fichiers[0] = argv[i];
			++nFichiers;
		}
	
	initB64();
	initSomme();
	initAvancee();
	srand(time(NULL));
	
	for(i = 0; i < nFichiers; ++i)
	{
		fchdir(fdIci);
		parcourir(& racine, fichiers[i]);
	}
	
	closedir(ici);
}
