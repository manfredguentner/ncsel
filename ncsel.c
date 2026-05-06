/* ncsel - a selection ncurses pager for terminals */
/* Copyright (c) 2026, Manfred Güntner             */
/* SPDX-License-Identifier: BSD-2-Clause           */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <ncurses.h>
#include <locale.h>
#include <sys/wait.h>
#include <fcntl.h>

/*  And so it begins... */

void usage(void) {
  printf("%s\n","usage: [-bhnsv][-f file] [file...]");
}

int readlines(FILE *tmp, char pagearray[][1024], int startline, int endline) {
/* read from filehandle from startline to endline into pagearray and returns lines read */

  char line[1024];
  int i = 0;
  int linesread = 0;

  fseek(tmp, 0, SEEK_SET);

  while (i < endline && fgets(line, sizeof(line), tmp) != NULL) {
    line[strcspn(line, "\r\n")] = '\0';

    if (i >= startline) {
      strcpy(pagearray[linesread], line);
      linesread++;
    }
    i++;
  }
  return linesread;
}

int main (int argc, char **argv) {
  
  int bflag = 0;
  int nflag = 0;
  int sflag = 0;
  int mflag = 0;
  int vflag = 0;
  int result = 0;
  chtype attr = A_BOLD;
  
  char *filename = NULL;
  int c;
  opterr = 0;

  while ((c = getopt (argc, argv, "bhmnsvf:")) != -1)
    switch (c)
      {
      case 'b':
        bflag = 1;
        attr = A_REVERSE;
        break;
      case 'h':
        usage();
        return 0;
      case 'm':
        mflag = 1;
        break;
      case 'n':
        nflag = 1;
        break;
      case 's':
        sflag = 1;
        break;
      case 'v':
        vflag = 1;
        break;
      case 'f':
         filename = optarg;
        break;
      case '?':
        if (optopt == 'f')
          printf ("Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          printf ("Unknown option `-%c'.\n", optopt);
        else
          printf ("Unknown option character `\\x%x'.\n",
                   optopt);
        return 1;
      default:
        abort ();
      }

  /* Read filename if not set by getopt */
  if (filename == NULL) { 
    filename = argv[optind];
  }
  if (filename == NULL) {
    usage();
    return 1;
  }

  /* tempfile, no array */
  FILE* tmp = tmpfile();
  if (tmp == NULL) {
    perror("tmpfile");
    return 1;
  }

  char line[1024];
  int linecount = 0;

  if (strcmp(filename, "-") == 0) {
    /* read from stdin */
    while (fgets(line, sizeof(line), stdin) != NULL ) {
       fprintf(tmp, "%s",line);
      linecount++;
    }
  }  else {
    /* read from file */
    FILE * fp = fopen(filename, "r");
    if (fp == NULL) {
        perror(filename);
        return -1;
    }
    /* try a seek to verify if file is empty */
    fseek(fp, 0, SEEK_END);
    if (ftell(fp) == 0) {
      printf("File is empty\n");
      fclose(fp);
      return 1;
    } else {
      rewind(fp);
    }
    /* write it all to tmpfile */
    while (fgets(line, sizeof(line), fp) != NULL ) {
      fprintf(tmp, "%s",line);
      linecount++;
    }
    fclose(fp);
  }

  /* action here */
  /* freopen tty to get stdin back for ncurses */
  FILE *tty = freopen("/dev/tty", "r+", stdin);
  if (tty == NULL) {
    perror("reopen /dev/tty");
    /*todo: maybe handle error */
  }

  /* init ncurses and reset filepointer tmp*/

  setlocale(LC_ALL, "");
  /* setlocale(LC_CTYPE, "C.UTF-8"); */

  /* we want to use /dev/tty for curses         */
  /* Otherwise var=$(ls . | ncsel - ) wont work */
  int ttyfd = open("/dev/tty", O_RDWR);
  FILE *ttyfp = NULL;
  SCREEN *scr = NULL;

  if (ttyfd >= 0) {
    ttyfp = fdopen(ttyfd, "r+");
    if (!ttyfp) { close(ttyfd); ttyfd = -1; }
  }

  if (ttyfp) {
    scr = newterm(NULL, ttyfp, ttyfp);
    if (!scr) {
      fprintf(stderr, "newterm failed\n");
      fclose(ttyfp);
      return 1;
    }
    set_term(scr);
  }  else {
    /* fallback */
    initscr();
  }

  noecho();
  keypad(stdscr, TRUE);
  if (bflag == 1) {
    curs_set(0);
  }
  rewind(tmp);

  char pagearray[1024][1024];
  int i = 0;
  int gch = 0; 

  struct Selections {
    int  lnum; 
    char mark[2];
    char line[1024];
  };
  int markcount = 0;  
  int maxmark = linecount;
  struct Selections *mark = malloc(maxmark * sizeof(*mark));
  if (!mark) {
    perror("malloc");
    return 1;
  }
  for (i = 0; i < maxmark; ++i) {
    mark[i].lnum = 0;
    mark[i].mark[0] = ' ';
    mark[i].mark[1] = '\0';
    mark[i].line[0] = '\0';
  }
  
  int arraylines = 0;
  int linesdone = 0;
  int paged = 0;
  int cpos = 0; /* cursor position */

  int rstart = 0;
  int rend = LINES - 1;

  while (1) {

    if (paged == 0) {
      arraylines = readlines(tmp, pagearray, rstart, rend);
      paged = 1;
      clear();
    }

    i = 0;

    do {
      if (i == cpos) {
        clrtoeol();
        attron(attr);    
        mvprintw(i,0,"%s%s%s",mark[linesdone+i].mark,pagearray[i]," ");
        attroff(attr);
      } else {
        clrtoeol();
        mvprintw(i,0,"%s%s",mark[linesdone+i].mark,pagearray[i]);
      }
      i++;
    } while (i < arraylines);

    mvprintw(arraylines,0,"%*s", COLS, "");
    attron(A_DIM);
    if (mflag == 0) {
      mvprintw(arraylines,0,"[%d/%d]",linesdone+cpos+1,linecount);
    } else {
      mvprintw(arraylines,0,"[%d/%d%s%d]",linesdone+cpos+1,linecount," m=",markcount);
    }
    move(cpos,0);
    attroff(A_DIM);
    /* refresh(); I leave it here, but it seems its not needed with no windows */

    gch  = getch();

    if (gch == '!') {

      echo();
      nocbreak();
      curs_set(1);

      char input[256];

      move(arraylines,0);
      clrtoeol();
      printw("!");
      refresh();

      getnstr(input, sizeof(input) - 1);

      noecho();
      cbreak();
      curs_set(0);
      def_prog_mode();
      endwin();

      char cmd[512];
      int idx = 0;
      int j;
      for (i = 0; input[i] != '\0' && idx < sizeof(cmd) - 1; i++) {
        if (input[i] == '%' && input[i+1] == 'S') {
          for (j = 0; pagearray[cpos][j] != '\0' && idx < sizeof(cmd) - 1; j++) {
            cmd[idx++] = pagearray[cpos][j];
          }
          i++; 
        } else {
          cmd[idx++] = input[i];
        }
      }

      cmd[idx] = '\0';

      pid_t pid = fork();
      if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
      } else if (pid > 0) {
        waitpid(pid, NULL, 0);
      }
  
      printf("!done  (press RETURN)");
      fflush(stdout);
      getchar();

      reset_prog_mode();
      refresh();
    }  
    if (gch == 'q') {
      result = 0;
      sflag = 0;
      nflag = 2;
      arraylines = cpos;
      break;
    }
    if (gch == 'x') {
      result = -1;
      sflag = 0;
      nflag = 2;
      arraylines = cpos;
      break;
    }
    if (gch == 'h') {
      clear();
      mvprintw(1,1,"%s", "f ^F SPACE PGDOWN = Forward  one window");
      mvprintw(2,1,"%s", "b ^B ESC-v PGUP   = Backward one window");
      mvprintw(3,1,"%s", "j ^N KEY_DOWN     = Forward  one line");
      mvprintw(4,1,"%s", "k ^B KEY_UP       = Backward one line");
      mvprintw(5,1,"%s", "  ");
      mvprintw(6,1,"%s", "Enter = select and exit");
      mvprintw(7,1,"%s", "q     = exit without selection (0)");
      mvprintw(8,1,"%s", "x     = exit without selection (-1)");
      mvprintw(9,1,"%s", "m s + = toggles marker if multiselect is on");
      mvprintw(10,1,"%s","!     = execute shell comand");
      mvprintw(11,1,"%s","        %S = line under the cursor");
      mvprintw(12,1,"%s","  ");
      mvprintw(13,1,"%s","Press any key to exit");
      /* refresh(); I leave it here, but... */
      getch();
      paged = 0;    
    }
    if (gch == 'm' || gch == '+' || gch == 's') {
      if (mflag == 1 ) {
        if (mark[linesdone+cpos].mark[0] == ' ') {
          mark[linesdone+cpos].mark[0] = '+';
          mark[linesdone+cpos].lnum = linesdone+cpos;
          snprintf(mark[linesdone+cpos].line, sizeof(mark[linesdone+cpos].line),"%s", pagearray[cpos]);
          markcount++;
        } else {
          mark[linesdone+cpos].mark[0] = ' ';
          mark[linesdone+cpos].lnum = 0;
          mark[linesdone+cpos].line[0] = '\0';
          markcount--;  
        }
      }
    }
    if (gch == KEY_DOWN || gch == 'k') {
      cpos++;
      if (cpos == arraylines) {
        if (linecount > linesdone + arraylines) {
          linesdone = linesdone + arraylines;
          paged = 0;
          cpos = 0;
          rstart = linesdone;
          rend =  linesdone + (LINES - 1);
        } else {
          cpos = 0;
        }
      }
    }
    if (gch == KEY_UP || gch == 'j') {
      cpos--;
      if (cpos == -1) {
        if (rstart > 0) {
          linesdone = linesdone - (LINES - 1);
          paged = 0;
          cpos = LINES - 2;
          rstart = rstart - (LINES - 1);
          rend = rend - (LINES - 1);
          if ( rstart < 0) {
            rstart = 0;
            rend = (LINES - 1);
          }
        } else {
          cpos = arraylines - 1;
        }
      }
    }
    if (gch == KEY_ENTER || gch == 10) {
      arraylines = cpos;
      break;      
    }
    if (gch == ' ' || gch == 32 || gch == 6 || gch == 'f' || gch == KEY_NPAGE) {
      if (linecount > linesdone + arraylines) {
        linesdone = linesdone + arraylines;
        paged = 0;
        cpos = 0;
        rstart = linesdone;
        rend =  linesdone + (LINES - 1);
      }    
    }
    if (gch == 'b' || gch == 2 || gch == 118 || gch == KEY_BACKSPACE || gch == KEY_PPAGE ) {
      if ( rstart > 0 ) {
        linesdone = linesdone - (LINES -1);
        paged = 0;
        cpos = 0;
        rstart = rstart - (LINES - 1);
        rend = rend - (LINES - 1);
        if ( rstart < 0) {
          rstart = 0;
          rend = (LINES - 1);
        }
      }
    }
    if (gch == KEY_RESIZE || gch == 410) {

      /* resizeterm(0, 0); todo: is a if-verification */
      clear();

      rend = LINES-1;
      paged = 0;
      if ( cpos > LINES - 1) {
        cpos = LINES - 1;
      }
      if ( cpos <= 2) {
        cpos = 1;
      }
    }
  } 
  /* end of inner loop */

  /* end of action */

  /* close all */
  fclose(tmp);
  curs_set(1);
  endwin();
  delscreen(scr);
  fclose(ttyfp);

  /* any markings ? */
  int marked = 0;
  if ( mflag == 1) {
    for (i = 0; i < maxmark; ++i) {
      if (mark[i].mark[0] != ' ') {
        marked = 1;
        break;
      }  
    }
  }

  /* markmode on but no selections */
  if (marked == 0 && mflag == 1) {
    sflag = 0;
    nflag = 2;
  }

  /* print result(s) */

  if (nflag == 1 && sflag == 1) {
    if (marked == 0) {
      printf("%d %s\n", linesdone+cpos+1, pagearray[arraylines]);
    } else {
      for (i = 0; i < maxmark; ++i) {
        if (mark[i].mark[0] == '+') {
          printf("%d %s\n", i+1, mark[i].line);
        }
      }
    }
  } else if (nflag == 0 && sflag == 0) {
    if (marked == 0) {
      printf("%d %s\n", linesdone+cpos+1, pagearray[arraylines]);
    } else {
      for (i = 0; i < maxmark; ++i) {
        if (mark[i].mark[0] == '+') {
          printf("%d %s\n", i+1, mark[i].line);
        }
      }
    }
  } else if (nflag == 1 && sflag == 0) {
    if (marked == 0) {
      printf("%d\n", linesdone+cpos+1);
    } else {
      for (i = 0; i < maxmark; ++i) {
        if (mark[i].mark[0] == '+') {
          printf("%d\n", i+1);
        }
      }
    }
  } else if (nflag == 0 && sflag == 1) {
    if (marked == 0) {
      printf("%s\n", pagearray[arraylines]);
    } else {
      for (i = 0; i < maxmark; ++i) {
        if (mark[i].mark[0] == '+') {
          printf("%s\n", mark[i].line);
        }
      }
    }
  } else if (nflag == 2 && sflag == 0) {
    if (vflag == 0) {
      printf("%d\n", result);
    } else {
      printf("%d %s\n", result, pagearray[arraylines]);
    }
  }

  /* done */
  free(mark);
  return 0;
} 
