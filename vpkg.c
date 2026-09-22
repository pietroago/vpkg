#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#define DB "/var/lib/vpkg"
#define INFO DB "/info"
#define FILES DB "/files"

#define LINE 1024

static int
run(char *
   const argv[]) {
   pid_t pid;
   int status;

   pid = fork();
   if (pid < 0)
      return -1;

   if (pid == 0) {
      execvp(argv[0], argv);
      _exit(127);
   }

   do {
      if (waitpid(pid, & status, 0) < 0) {
         if (errno == EINTR)
            continue;
         return -1;
      }
      break;
   } while (1);

   if (!WIFEXITED(status))
      return -1;

   return WEXITSTATUS(status);
}

static int
mkdirp(const char * path) {
   char buf[PATH_MAX];
   char * p;

   if (strlen(path) >= sizeof(buf))
      return -1;

   strcpy(buf, path);

   for (p = buf + 1;* p; p++) {
      if ( * p != '/')
         continue;

      * p = '\0';
      if (mkdir(buf, 0755) < 0 && errno != EEXIST)
         return -1;
      * p = '/';
   }

   if (mkdir(buf, 0755) < 0 && errno != EEXIST)
      return -1;

   return 0;
}

static char *
   field(const char * path,
      const char * key) {
      FILE * fp;
      char line[LINE];
      size_t len;
      char * p;

      fp = fopen(path, "r");
      if (!fp)
         return NULL;

      len = strlen(key);

      while (fgets(line, sizeof(line), fp)) {
         if (strncmp(line, key, len) != 0 || line[len] != '=')
            continue;

         p = strdup(line + len + 1);
         fclose(fp);

         if (!p)
            return NULL;

         p[strcspn(p, "\r\n")] = '\0';
         return p;
      }

      fclose(fp);
      return NULL;
   }

static int
build(const char * dir,
   const char * pkg) {
   char cwd[PATH_MAX];
   char out[PATH_MAX];
   char * argv[] = {
      "tar",
      "-C",
      (char * ) dir,
      "-cf",
      out,
      ".",
      NULL
   };

   if (!getcwd(cwd, sizeof(cwd)))
      return -1;

   if (pkg[0] == '/') {
      if (snprintf(out, sizeof(out), "%s", pkg) >=
         (int) sizeof(out))
         return -1;
   } else {
      if (snprintf(out, sizeof(out), "%s/%s", cwd, pkg) >=
         (int) sizeof(out))
         return -1;
   }

   return run(argv);
}

static int
extract_info(const char * pkg,
   const char * out) {
   int fd;
   pid_t pid;
   int status;
   char * argv[] = {
      "tar",
      "-xOf",
      (char * ) pkg,
      "./.vpkg/info",
      NULL
   };

   fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd < 0)
      return -1;

   pid = fork();
   if (pid < 0) {
      close(fd);
      return -1;
   }

   if (pid == 0) {
      if (dup2(fd, STDOUT_FILENO) < 0)
         _exit(127);

      close(fd);
      execvp(argv[0], argv);
      _exit(127);
   }

   close(fd);

   do {
      if (waitpid(pid, & status, 0) < 0) {
         if (errno == EINTR)
            continue;
         return -1;
      }
      break;
   } while (1);

   if (!WIFEXITED(status))
      return -1;

   return WEXITSTATUS(status);
}

static int
save_files(const char * pkg,
   const char * path) {
   FILE * fp;
   FILE * out;
   char line[LINE];
   int pipefd[2];
   pid_t pid;
   int status;
   char * argv[] = {
      "tar",
      "-tf",
      (char * ) pkg,
      NULL
   };

   if (pipe(pipefd) < 0)
      return -1;

   pid = fork();
   if (pid < 0) {
      close(pipefd[0]);
      close(pipefd[1]);
      return -1;
   }

   if (pid == 0) {
      close(pipefd[0]);

      if (dup2(pipefd[1], STDOUT_FILENO) < 0)
         _exit(127);

      close(pipefd[1]);
      execvp(argv[0], argv);
      _exit(127);
   }

   close(pipefd[1]);

   fp = fdopen(pipefd[0], "r");
   if (!fp) {
      close(pipefd[0]);
      return -1;
   }

   out = fopen(path, "w");
   if (!out) {
      fclose(fp);
      return -1;
   }

   while (fgets(line, sizeof(line), fp)) {
      if (strncmp(line, "./.vpkg/", 8) == 0)
         continue;

      fputs(line, out);
   }

   fclose(fp);
   fclose(out);

   do {
      if (waitpid(pid, & status, 0) < 0) {
         if (errno == EINTR)
            continue;
         return -1;
      }
      break;
   } while (1);

   if (!WIFEXITED(status))
      return -1;

   return WEXITSTATUS(status);
}

static int
install(const char * pkg) {
   char tmp[] = "/tmp/vpkg-info-XXXXXX";
   char path[PATH_MAX];
   char files[PATH_MAX];
   char *name = NULL;
   char *version = NULL;
   int fd;
   char * argv[] = {
      "tar",
      "-xf",
      (char * ) pkg,
      "-C",
      "/",
      "--exclude=./.vpkg",
      NULL
   };

   fd = mkstemp(tmp);
   if (fd < 0)
      return -1;

   close(fd);

   if (extract_info(pkg, tmp) != 0)
      goto fail;

   name = field(tmp, "name");
   version = field(tmp, "version");

   if (!name || ! * name)
      goto fail;

   if (mkdirp(INFO) < 0 || mkdirp(FILES) < 0)
      goto fail;

   if (snprintf(path, sizeof(path), "%s/%s", INFO, name) >=
      (int) sizeof(path))
      goto fail;

   if (snprintf(files, sizeof(files), "%s/%s", FILES, name) >=
      (int) sizeof(files))
      goto fail;

   if (save_files(pkg, files) != 0)
      goto fail;

   if (run(argv) != 0)
      goto fail;

   {
      FILE * src;
      FILE * dst;
      char buf[4096];
      size_t n;

      src = fopen(tmp, "r");
      dst = fopen(path, "w");

      if (!src || !dst) {
         if (src)
            fclose(src);
         if (dst)
            fclose(dst);
         goto fail;
      }

      while ((n = fread(buf, 1, sizeof(buf), src)) != 0) {
         if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            goto fail;
         }
      }

      fclose(src);
      fclose(dst);
   }

   unlink(tmp);

   printf("installed %s", name);
   if (version)
      printf("-%s", version);
   putchar('\n');

   free(name);
   free(version);

   return 0;

   fail:
      unlink(tmp);
   free(name);
   free(version);
   return -1;
}

static int
remove_package(const char * name) {
   char path[PATH_MAX];
   FILE * fp;
   char line[LINE];

   if (snprintf(path, sizeof(path), "%s/%s", FILES, name) >=
      (int) sizeof(path))
      return -1;

   fp = fopen(path, "r");
   if (!fp) {
      fprintf(stderr, "vpkg: %s is not installed\n", name);
      return -1;
   }

   while (fgets(line, sizeof(line), fp)) {
      char * p = line;

      p[strcspn(p, "\r\n")] = '\0';

      if (! * p || strcmp(p, ".") == 0)
         continue;

      if (strncmp(p, "./", 2) != 0)
         continue;

      unlink(p + 1);
   }

   fclose(fp);

   unlink(path);

   if (snprintf(path, sizeof(path), "%s/%s", INFO, name) >=
      (int) sizeof(path))
      return -1;

   unlink(path);

   printf("removed %s\n", name);
   return 0;
}

static int
query(const char * name) {
   char path[PATH_MAX];
   FILE * fp;
   char line[LINE];

   if (snprintf(path, sizeof(path), "%s/%s", INFO, name) >=
      (int) sizeof(path))
      return -1;

   fp = fopen(path, "r");
   if (!fp) {
      fprintf(stderr, "vpkg: %s is not installed\n", name);
      return -1;
   }

   while (fgets(line, sizeof(line), fp))
      fputs(line, stdout);

   fclose(fp);
   return 0;
}

static int
list(void) {
   DIR * dir;
   struct dirent * ent;

   dir = opendir(INFO);
   if (!dir) {
      if (errno == ENOENT)
         return 0;
      return -1;
   }

   while ((ent = readdir(dir))) {
      if (ent -> d_name[0] == '.')
         continue;

      puts(ent -> d_name);
   }

   closedir(dir);
   return 0;
}

static void
usage(void) {
   puts(
      "vpkg\n"
      "  -b DIR PKG   build\n"
      "  -i PKG       install\n"
      "  -r NAME      remove\n"
      "  -q NAME      query\n"
      "  -l           list"
   );
}

int
main(int argc, char ** argv) {
   if (argc < 2) {
      usage();
      return 1;
   }

   if (!strcmp(argv[1], "-b") && argc == 4)
      return build(argv[2], argv[3]) != 0;

   if (!strcmp(argv[1], "-i") && argc == 3)
      return install(argv[2]) != 0;

   if (!strcmp(argv[1], "-r") && argc == 3)
      return remove_package(argv[2]) != 0;

   if (!strcmp(argv[1], "-q") && argc == 3)
      return query(argv[2]) != 0;

   if (!strcmp(argv[1], "-l") && argc == 2)
      return list() != 0;

   usage();
   return 1;
}
