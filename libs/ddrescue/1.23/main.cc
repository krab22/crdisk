/*  GNU ddrescue - Data recovery tool
    Copyright (C) 2004-2018 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
    Exit status:
    0 for a normal exit,
    1 for environmental problems (file not found, invalid flags, I/O errors, etc),
    2 to indicate a corrupt or invalid input file,
    3 for an internal consistency error (eg, bug) which caused ddrescue to panic.
*/

#define _FILE_OFFSET_BITS 64
#ifndef PROGVERSION
#define PROGVERSION "1.23"
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "arg_parser.h"
#include "rational.h"
#include "block.h"
#include "loggers.h"
#include "mapbook.h"
#include "non_posix.h"
#include "rescuebook.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

// http://www.k-max.name/linux/ddrescue-hdd-vosstanovlenie-and-examples/

namespace {

const char * const Program_name = "GNU ddrescue";
const char * const program_name = "ddrescue";
const char * invocation_name = 0;

enum Mode { m_none, m_fill, m_generate };

const mode_t outmode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

void show_help( const int cluster, const int hardbs )
  {
  std::printf( "%s - Инструмент восстановления данных.\n", Program_name );
  std::printf( "Копирует данные из одного файла или блочного устройства в другое,\n"
               "сначала попытаться спасти хорошие части в случае ошибок чтения.\n"
               "\nИспользование: %s [опции] файл-источник файл-приемник [mapfile]\n", invocation_name );
  std::printf( "\nВсегда используйте файле проекта, если вы не знаете, что вы не будете нуждаться в этом. Без\n"
               "него, ddrescue не может возобновить спасение, только возобновить его.\n"
               "ПРИМЕЧАНИЕ. В версиях ddrescue до 1.20 был вызван файл карты\n"
               "'logfile'. Формат тот же; только имя изменилось.\n"
               "\nЕсли вы перезагрузитесь, проверьте имена устройств перед рестартом ddrescue.\n"
               "Не используйте опции '-F' or '-G' не прочитав сначала руководства.\n"
               "\nОпции:\n"
               "  -h, --help                     вывести помощь и выйти\n"
               "  -V, --version                  вывести информацию о версии и выйти\n"
               "  -a, --min-read-rate=<bytes>    минимальная скорость чтения хороших областей в байтах в секунду\n"
               "  -A, --try-again                маркировать без обрезки, без царапин, как не проверенные\n"
               "  -b, --sector-size=<bytes>      размер сектора устройства ввода [по умолчанию %d]\n", hardbs );
  std::printf( "  -B, --binary-prefixes          показать бинарные множители в числах [SI]\n"
               "  -c, --cluster-size=<sectors>   количество одновременно копируемых секторов [%d]\n", cluster );
  std::printf( "  -C, --complete-only            не читать новые данные за пределами ограничений файлового поля\n"
               "  -d, --idirect                  использовать прямой доступ к диску для входного файла\n"
               "  -D, --odirect                  использовать прямой доступ к диску для выходного файла\n"
               "  -e, --max-bad-areas=[+]<n>     максимальное допустимое количество [новых]\n"
               "  -E, --max-error-rate=<bytes>   максимально допустимая скорость чтения ошибок в секунду\n"
               "  -f, --force                    перезаписать выходное устройство или раздел\n"
               "  -F, --fill-mode=<types>        заполнять блоки данных типов данными (?*/-+l)\n"
               "  -G, --generate-mode            генерировать приблизительный файл карты из частичной копии\n"
               "  -H, --test-mode=<file>         установить карту хороших / плохих блоков из заданного файла карты\n"
               "  -i, --input-position=<bytes>   начальная позиция домена во входном файле [0]\n"
               "  -I, --verify-input-size        проверить размер входного файла с размером в mapfile\n"
               "  -J, --verify-on-error          перечитать последний хороший сектор после каждой ошибки\n"
               "  -K, --skip-size=[<i>][,<max>]  начальный, максимальный размер, чтобы пропустить ошибку чтения\n"
               "  -L, --loose-domain             принять неполный файл карты домена\n"
               "  -m, --domain-mapfile=<file>    ограничить домен до готовых блоков в <файле>\n"
               "  -M, --retrim                   отметьте все поврежденные блоки как необрезанные\n"
               "  -n, --no-scrape                пропустить этап очистки\n"
               "  -N, --no-trim                  пропустить этап обрезки\n"
               "  -o, --output-position=<bytes>  начальная позиция в выходном файле [ipos]\n"
               "  -O, --reopen-on-error          повторно открыть входной файл после каждой ошибки чтения\n"
               "  -p, --preallocate              prealocate space на диске для выходного файла\n"
               "  -P, --data-preview[=<lines>]   показать некоторые строки последних прочитанных данных [3]\n"
               "  -q, --quiet                    подавлять все сообщения\n"
               "  -r, --retry-passes=<n>         выйти после <n> повторных проходов (-1 = бесконечность) [0]\n"
               "  -R, --reverse                  изменить направление всех проходов\n"
               "  -s, --size=<bytes>             максимальный размер входных данных для копирования\n"
               "  -S, --sparse                   использовать разреженные записи для выходного файла\n"
               "  -t, --truncate                 обрезать выходной файл до нулевого размера\n"
               "  -T, --timeout=<interval>       максимальное время с момента последнего успешного чтения\n"
               "  -u, --unidirectional           запускать все проходы в одном направлении\n"
               "  -v, --verbose                  быть многословным (второй -v дает больше)\n"
               "  -w, --ignore-write-errors      сделать режим заполнения игнорировать ошибки записи\n"
               "  -x, --extend-outfile=<bytes>   расширьте размер файла, чтобы быть как минимум длинным\n"
               "  -X, --max-read-errors=<n>      допустимо максимальное количество ошибок чтения\n"
               "  -y, --synchronous              использовать синхронную запись для выходного файла\n"
               "  -Z, --max-read-rate=<bytes>    максимальная скорость чтения в байтах/с\n"
               "      --ask                      запросить подтверждение перед началом копирования\n"
               "      --cpass=<n>[,<n>]          выберите, какие копии будут выполняться для запуска\n"
               "      --delay-slow=<interval>    начальная задержка перед проверкой медленного чтения [30]\n"
               "      --log-events=<file>        регистрировать значительные события в <файле>\n"
               "      --log-rates=<file>         скорости регистрации и размеры ошибок в <файле>\n"
               "      --log-reads=<file>         записывать все операции чтения в <файл>\n"
               "      --mapfile-interval=[i][,i]   сохранять / синхронизировать файл карты с заданным интервалом [авто]\n"
               "      --max-slow-reads=<n>         максимальное допустимое число медленных чтений\n"
               "      --pause-on-error=<interval>  время ожидания после каждой ошибки чтения [0]\n"
               "      --pause-on-pass=<interval>   время ожидания между проходами [0]\n"
               "      --reset-slow               сброс медленного чтения, если скорость повышается выше min\n"
               "      --same-file                разрешить infile и outfile быть одним и тем же файлом\n"
               "Числа могут быть десятичными, шестнадцатеричными или восьмеричными, и за ними может следовать\n"
               "умножитель: s = сектора, k = 1000, Ki = 1024, M = 10 ^ 6, Mi = 2 ^ 20 и т. д. \n "
               "Временные интервалы имеют формат 1 [.5] [smhd] или 1/2 [smhd].\n"
               "\nСтатус выхода: 0 для нормального выхода, 1 для проблем с окружающей \n"
               "средой (файл не найден, недопустимые флаги, ошибки ввода-вывода и т. Д.), \n"
               "2 для указания поврежденного или недопустимого входного файла, 3 для \n"
               "внутренней ошибки согласованности (например, ошибка ), что вызвало панику."
               "\nСообщить об ошибках в bug-ddrescue@gnu.org\n"
               "Домашняя страница Ddrescue: http://www.gnu.org/software/ddrescue/ddrescue.html\n"
               "Общая справка с использованием программного обеспечения GNU: http://www.gnu.org/gethelp\n" );
  }

// Признанные форматы: <rational_number> [unit]
// Если дополнительный «блок» является одним из «s», «m», «h» или «d»,.
// Возвращает количество секунд или выходит с 1 статусом, если ошибка.
//
Rational parse_rational_time( const char * const ptr,
                              const bool comma = false,
                              const int max_den = 100 )
  {
  Rational r;
  int c = r.parse( ptr );

  if( c > 0 )
    {
    bool error = false;
    switch( ptr[c] )
      {
      case 'd': r *= 86400; break;			// 24 * 60 * 60
      case 'h': r *= 3600; break;			// 60 * 60
      case 'm': r *= 60; break;
      case 's':
      case  0 : break;
      case ',': if( comma ) break;
      default : error = true;
      }
    if( error || ( !comma && ptr[c] != 0 && ptr[c] != ',' && ptr[c+1] == ',' ) )
      { show_error( "Bad unit in time interval.", 0, true ); std::exit( 1 ); }
    if( !r.error() && r >= 0 && r.denominator() <= max_den ) return r;
    }
  show_error( "Bad value for time interval.", 0, true );
  std::exit( 1 );
  }

int parse_time_interval( const char * const ptr,
                         const bool comma = false )
  { return parse_rational_time( ptr, comma, 1 ).trunc(); }

bool check_identical( const char * const iname,
                      const char * const oname,
                      const char * const mapname,
                      const bool same_file )
  {
  struct stat istat, ostat, mapstat;
  bool iexists = false, oexists = false, mapexists = false;
  bool same = ( std::strcmp( iname, oname ) == 0 );
  if( !same )
    {
    iexists = ( stat( iname, &istat ) == 0 );
    oexists = ( stat( oname, &ostat ) == 0 );
    if( iexists && oexists && istat.st_ino == ostat.st_ino &&
        istat.st_dev == ostat.st_dev ) same = true;
    }
  if( same && !same_file )
    { show_error( "Infile and outfile are the same." ); return true; }
  if( mapname )
    {
    same = ( std::strcmp( iname, mapname ) == 0 );
    if( !same )
      {
      mapexists = ( stat( mapname, &mapstat ) == 0 );
      if( iexists && mapexists && istat.st_ino == mapstat.st_ino &&
          istat.st_dev == mapstat.st_dev ) same = true;
      }
    if( same )
      { show_error( "Infile and mapfile are the same." ); return true; }
    if( std::strcmp( oname, mapname ) == 0 ||
        ( oexists && mapexists && ostat.st_ino == mapstat.st_ino &&
          ostat.st_dev == mapstat.st_dev ) )
      { show_error( "Outfile and mapfile are the same." ); return true; }
    }
  return false;
  }

bool check_files( const char * const iname,
                  const char * const oname,
                  const char * const mapname,
                  const Rb_options & rb_opts,
                  const bool force,
                  const bool generate,
                  const bool preallocate )
  {
  if( !iname || !oname )
    {
    show_error( "Both input and output files must be specified.", 0, true );
    return false;
    }
  if( check_identical( iname, oname, mapname, rb_opts.same_file ) )
    return false;
  if( mapname )
    {
    struct stat st;
    if( stat( mapname, &st ) == 0 && !S_ISREG( st.st_mode ) )
      {
      show_error( "Mapfile exists and is not a regular file." );
      return false;
      }
    }
  if( !generate && ( rb_opts.min_outfile_size > 0 || !force ||
      preallocate || rb_opts.sparse ) )
    {
    struct stat st;
    if( stat( oname, &st ) == 0 && !S_ISREG( st.st_mode ) )
      {
      show_error( "Output file exists and is not a regular file." );
      if( !force )
        show_error( "Use '--force' if you really want to overwrite it, but be\n"
                    "          aware that all existing data in the output file will be lost.",
                    0, true );
      else if( rb_opts.min_outfile_size > 0 )
        show_error( "Only regular files can be extended.", 0, true );
      else if( preallocate )
        show_error( "Only regular files can be preallocated.", 0, true );
      else if( rb_opts.sparse )
        show_error( "Only regular files can be sparse.", 0, true );
      return false;
      }
    }
  return true;
  }

int do_fill( const long long offset,
             Domain & domain,
             const Fb_options & fb_opts,
             const Mb_options & mb_opts,
             const char * const iname,
             const char * const oname,
             const char * const mapname,
             const int cluster,
             const int hardbs,
             const int o_direct_out,
             const bool synchronous )
  {
  if( !mapname )
    { show_error( "Mapfile required in fill mode.", 0, true ); return 1; }

  Fillbook fillbook( offset, domain, fb_opts, mb_opts, mapname, cluster,
                     hardbs, synchronous );
  if( !fillbook.mapfile_exists() ) return not_readable( mapname );
  if( fillbook.domain().empty() ) return empty_domain();
  if( fillbook.read_only() ) return not_writable( mapname );

  const int ides = open( iname, O_RDONLY | O_BINARY );
  if( ides < 0 )
    { show_error( "Can't open input file", errno ); return 1; }
  if( !fillbook.read_buffer( ides ) )
    { show_error( "Error reading fill data from input file", errno ); return 1; }
  if( close( ides ) != 0 )
    { show_error( "Error closing infile", errno ); return 1; }

  const int odes = open( oname, O_CREAT | O_WRONLY | o_direct_out | O_BINARY,
                         outmode );
  if( odes < 0 )
    { show_error( "Can't open output file", errno ); return 1; }
  if( lseek( odes, 0, SEEK_SET ) )
    { show_error( "Output file is not seekable." ); return 1; }

  if( verbosity >= 0 )
    std::printf( "%s %s\n", Program_name, PROGVERSION );
  if( verbosity >= 1 )
    {
    std::printf( "About to fill with data from %s blocks of %s marked %s\n",
                 iname, oname, fb_opts.filltypes.c_str() );
    std::printf( "    Maximum size to fill: %sBytes\n",
                 format_num( fillbook.domain().in_size() ) );
    std::printf( "    Starting positions: infile = %sB,  outfile = %sB\n",
                 format_num( fillbook.domain().pos() ),
                 format_num( fillbook.domain().pos() + fillbook.offset() ) );
    std::printf( "    Copy block size: %3d sectors\n", cluster );
    std::printf( "Sector size: %sBytes\n", format_num( hardbs, 99999 ) );
    std::printf( "Direct out: %s\n\n", o_direct_out ? "yes" : "no" );
    }

  return fillbook.do_fill( odes );
  }

int do_generate( const long long offset,
                 Domain & domain,
                 const Mb_options & mb_opts,
                 const char * const iname,
                 const char * const oname,
                 const char * const mapname,
                 const int cluster,
                 const int hardbs )
  {
  if( !mapname )
    {
    show_error( "Mapfile must be specified in generate mode.", 0, true );
    return 1;
    }

  const int ides = open( iname, O_RDONLY | O_BINARY );
  if( ides < 0 )
    { show_error( "Can't open input file", errno ); return 1; }
  const long long isize = lseek( ides, 0, SEEK_END );
  if( isize < 0 )
    { show_error( "Input file is not seekable." ); return 1; }

  Genbook genbook( offset, isize, domain, mb_opts, mapname, cluster, hardbs );
  if( genbook.domain().empty() ) return empty_domain();
  if( !genbook.blank() && genbook.current_status() != Mapfile::generating )
    {
    show_error( "Mapfile alredy exists and is not empty.", 0, true );
    return 1;
    }
  if( genbook.read_only() ) return not_writable( mapname );

  const int odes = open( oname, O_RDONLY | O_BINARY );
  if( odes < 0 )
    { show_error( "Can't open output file", errno ); return 1; }
  if( lseek( odes, 0, SEEK_SET ) )
    { show_error( "Output file is not seekable." ); return 1; }

  if( verbosity >= 0 )
    std::printf( "%s %s\n", Program_name, PROGVERSION );
  if( verbosity >= 1 )
    {
    std::printf( "About to generate an approximate mapfile for %s and %s\n",
                 iname, oname );
    std::printf( "    Starting positions: infile = %sB,  outfile = %sB\n",
                 format_num( genbook.domain().pos() ),
                 format_num( genbook.domain().pos() + genbook.offset() ) );
    std::printf( "    Copy block size: %3d sectors\n", cluster );
    std::printf( "Sector size: %sBytes\n\n", format_num( hardbs, 99999 ) );
    }
  return genbook.do_generate( odes );
  }

void device_id_and_size( const long long size,
                         const int fd,
                         std::string & id_str )
  {
  struct stat st;
  if( fstat( fd, &st ) == 0 && S_ISBLK( st.st_mode ) )
    {
    if( device_id( fd, id_str ) ) { id_str.insert( 0, " [" ); id_str += ']'; }
    else id_str = " [UNKNOWN]";
    }
  char buf[32];
  snprintf( buf, sizeof buf, " (%lld)", size );
  id_str += buf;
  }

void about_to_copy( const Rescuebook & rescuebook,
                    const char * const iname,
                    const char * const oname,
                    const long long isize,
                    const int ides,
                    const bool ask )
  {
  if( ask || verbosity >= 0 )
    std::printf( "%s %s\n", Program_name, PROGVERSION );
  if( ask || verbosity >= 1 )
    {
    std::string iid, oid;
    char c = ' '; const char * p = " ";		// show on one line
    if( ask || verbosity >= 2 )
      {
      device_id_and_size( isize, ides, iid );
      const int odes = open( oname, O_RDONLY );
      if( odes >= 0 )
        {
        device_id_and_size( lseek( odes, 0, SEEK_END ), odes, oid );
        close( odes );
        }
      if( ask ) { c = '\n'; p = "\n  "; }	// show on separate lines
      }
    std::printf( "About to copy %sBytes%cfrom '%s'%s%sto '%s'%s\n",
                 rescuebook.domain().full() ? "an unknown number of " :
                   format_num( rescuebook.domain().in_size() ),
                 c, iname, iid.c_str(), p, oname, oid.c_str() );
    }
  }

bool user_agrees_ids( const Rescuebook & rescuebook,
                      const char * const iname,
                      const char * const oname,
                      const long long isize,
                      const int ides )
  {
  about_to_copy( rescuebook, iname, oname, isize, ides, true );
  std::fputs( "Proceed (y/N)? ", stdout );
  std::fflush( stdout );
  return ( std::tolower( std::fgetc( stdin ) ) == 'y' );
  }

long long adjusted_isize( const int ides,
                          const Domain * const test_domain )
  {
  long long isize = lseek( ides, 0, SEEK_END );
  if( isize >= 0 && test_domain )
    {
    const long long size = test_domain->end();
    if( isize <= 0 || isize > size ) isize = size;
    }
  return isize;
  }

int do_rescue( const long long offset,
               Domain & domain,
               const Domain * const test_domain,
               const Mb_options & mb_opts,
               const Rb_options & rb_opts,
               const char * const iname,
               const char * const oname,
               const char * const mapname,
               const int cluster,
               const int hardbs,
               const int o_direct_out,
               const int o_trunc,
               const bool ask,
               const bool preallocate,
               const bool synchronous,
               const bool verify_input_size )
  {
  if( rb_opts.same_file && o_trunc )
    {
    show_error( "Опция '--same-file' не совместима с '--truncate'.", 0, true );
    return 1;
    }

  // use same flags as reopen_infile
  const int ides = open( iname, O_RDONLY | rb_opts.o_direct_in | O_BINARY );
  if( ides < 0 )
    { show_error( "Невозможно открыть входной файл", errno ); return 1; }
  const long long isize = adjusted_isize( ides, test_domain );
  if( isize < 0 )
    { show_error( "Входной файл не доступен для поиска." ); return 1; }

  Rescuebook rescuebook( offset, isize, domain, test_domain, mb_opts, rb_opts,
                         iname, mapname, cluster, hardbs, synchronous );

  if( verify_input_size )
    {
    if( !rescuebook.mapfile_exists() || isize <= 0 ||
        rescuebook.mapfile_isize() <= 0 ||
        rescuebook.mapfile_isize() >= LLONG_MAX )
      {
      show_error( "Can't verify input file size.\n"
                  "          Mapfile is unfinished or missing or size is invalid." );
      return 1;
      }
    if( rescuebook.mapfile_isize() != isize )
      {
      show_error( "Input file size differs from size calculated from mapfile." );
      return 1;
      }
    }
  if( rescuebook.domain().empty() )
    {
    if( rescuebook.complete_only && !rescuebook.mapfile_exists() )
      { show_error( "Nothing to complete; mapfile is missing or empty.", 0, true );
        return 1; }
    return empty_domain();
    }
  if( o_trunc && !rescuebook.blank() )
    {
    show_error( "Outfile truncation and mapfile input are incompatible.", 0, true );
    return 1;
    }
  if( rescuebook.read_only() ) return not_writable( mapname );

  if( ask && !user_agrees_ids( rescuebook, iname, oname, isize, ides ) )
    return 1;

  const int odes = open( oname, O_CREAT | O_WRONLY | o_direct_out |
                         o_trunc | O_BINARY, outmode );
  if( odes < 0 )
    { show_error( "Can't open output file", errno ); return 1; }
  if( lseek( odes, 0, SEEK_SET ) )
    { show_error( "Output file is not seekable." ); return 1; }
  if( preallocate && lseek( odes, 0, SEEK_END ) - rescuebook.offset() <
                     rescuebook.domain().end() )
    {
#if defined _POSIX_ADVISORY_INFO && _POSIX_ADVISORY_INFO > 0
    if( posix_fallocate( odes, rescuebook.domain().pos() + rescuebook.offset(),
                         rescuebook.domain().size() ) != 0 )
      { show_error( "Can't preallocate output file", errno ); return 1; }
#else
    show_error( "warning: Preallocation not available." );
#endif
    }

  if( rescuebook.filename() && !rescuebook.mapfile_exists() &&
      !rescuebook.write_mapfile( 0, true ) )
    { show_error( "Can't create mapfile", errno ); return 1; }

  if( !event_logger.open_file() )
    { show_error( "Can't open file for logging events", errno ); return 1; }
  if( !rate_logger.open_file() )
    { show_error( "Can't open file for logging rates", errno ); return 1; }
  if( !read_logger.open_file() )
    { show_error( "Can't open file for logging reads", errno ); return 1; }

  if( !ask ) about_to_copy( rescuebook, iname, oname, isize, ides, false );
  if( verbosity >= 1 )
    {
    std::printf( "    Starting positions: infile = %sB,  outfile = %sB\n",
                 format_num( rescuebook.domain().pos() ),
                 format_num( rescuebook.domain().pos() + rescuebook.offset() ) );
    std::printf( "    Copy block size: %3d sectors", cluster );
    if( rescuebook.skipbs > 0 )
      std::printf( "       Initial skip size: %lld sectors\n",
                   rescuebook.skipbs / hardbs );
    else
      std::fputs( "       Skipping disabled\n", stdout );
    std::printf( "Sector size: %sBytes\n", format_num( hardbs, 99999 ) );
    if( verbosity >= 2 )
      {
      bool nl = false;
      if( rescuebook.max_error_rate >= 0 )
        { nl = true; std::printf( "Max error rate: %6sB/s    ",
                                  format_num( rescuebook.max_error_rate, 99999 ) ); }
      if( rescuebook.max_bad_areas < ULONG_MAX )
        {
        nl = true;
        std::printf( "Max %sbad areas: %lu    ", rescuebook.new_bad_areas_only ?
                     "new " : "", rescuebook.max_bad_areas );
        }
      if( nl ) { nl = false; std::fputc( '\n', stdout ); }

      if( rescuebook.max_read_rate > 0 )
        { nl = true; std::printf( "Max read rate:  %6sB/s    ",
                                  format_num( rescuebook.max_read_rate, 99999 ) ); }
      if( rescuebook.min_read_rate == 0 )
        { nl = true; std::fputs( "Min read rate: auto    ", stdout ); }
      else if( rescuebook.min_read_rate > 0 )
        { nl = true; std::printf( "Min read rate:  %6sB/s    ",
                                  format_num( rescuebook.min_read_rate, 99999 ) ); }
      if( nl ) { nl = false; std::fputc( '\n', stdout ); }

      if( rescuebook.timeout >= 0 )
        { nl = true; std::printf( "Timeout: %-7s  ",
                                  format_time( rescuebook.timeout ) ); }
      if( rescuebook.pause_on_error > 0 )
        { nl = true; std::printf( "Pause on error: %ss  ",
                                  rescuebook.pause_on_error.to_decimal().c_str() ); }
      if( rescuebook.pause_on_pass > 0 )
        { nl = true; std::printf( "Pause on pass: %s",
                                  format_time( rescuebook.pause_on_pass ) ); }
      if( nl ) { nl = false; std::fputc( '\n', stdout ); }

      std::printf( "Direct in: %s    ", rescuebook.o_direct_in ? "yes" : "no " );
      std::printf( "Direct out: %s    ", o_direct_out ? "yes" : "no " );
      std::printf( "Sparse: %s    ", rescuebook.sparse ? "yes" : "no " );
      std::printf( "Truncate: %s    ", o_trunc ? "yes" : "no " );
      std::fputc( '\n', stdout );
      std::printf( "Trim: %s         ", !rescuebook.notrim ? "yes" : "no " );
      std::printf( "Scrape: %s        ", !rescuebook.noscrape ? "yes" : "no " );
      if( rescuebook.max_retries >= 0 )
          std::printf( "Максимально повторных проходов: %d", rescuebook.max_retries );
      std::fputc( '\n', stdout );
      if( rescuebook.complete_only )
        { nl = true; std::fputs( "Только для завершения    ", stdout ); }
      if( rescuebook.reverse )
        { nl = true; std::fputs( "Обратный режим", stdout ); }
      if( nl ) { nl = false; std::fputc( '\n', stdout ); }
      }
    std::fputc( '\n', stdout );
    }
  return rescuebook.do_rescue( ides, odes );
  }

} // end namespace

#include "main_common.cc"

namespace {

void parse_cpass( const char * p,
                  Rb_options & rb_opts )
  {
  rb_opts.cpass_bitset = 0;
  if( *p == '0' ) { if( p[1] == 0 ) return; }
  else while( true )
    {
    const unsigned char ch1 = *p++;
    if( ch1 < '1' || ch1 > '5' ) break;
    if( *p != '-' ) rb_opts.cpass_bitset |= ( 1 << ( ch1 - '1' ) );
    else
      {
      ++p;
      if( *p < '1' || *p > '5' || ch1 > *p ) break;
      for( int c = ch1; c <= *p; ++c )
        rb_opts.cpass_bitset |= ( 1 << ( c - '1' ) );
      ++p;
      }
    if( *p == 0 ) return;
    if( *p == ',' ) ++p; else break;
    }
  show_error( "Недопустимый проход или диапазон проходов в опции '--cpass'" );
  std::exit( 1 );
  }


void parse_mapfile_intervals( const char * const ptr,
                              Mb_options & mb_opts )
  {
  const char * const ptr2 = std::strchr( ptr, ',' );

  if( !ptr2 || ptr2 != ptr )
    {
    if( ptr[0] == '-' && ptr[1] == '1' && ( ptr[2] == 0 || ptr[2] == ',' ) )
      mb_opts.mapfile_save_interval = -1;
    else mb_opts.mapfile_save_interval = parse_time_interval( ptr, true );
    }
  if( ptr2 )
    mb_opts.mapfile_sync_interval = parse_time_interval( ptr2 + 1 );
  if( mb_opts.mapfile_sync_interval < 5 )
    {
    show_error( "Минимальный 'mapfile sync interval' 5 секунд." );
    std::exit( 1 );
    }
  if( mb_opts.mapfile_save_interval > mb_opts.mapfile_sync_interval )
    {
    show_error( "'mapfile save interval' больше чем 'mapfile sync interval'." );
    std::exit( 1 );
    }
  }


void parse_pause_on_error( const char * const p,
                           Rb_options & rb_opts )
  {
  rb_opts.simulated_poe = ( p[0] == 's' );
  if( rb_opts.simulated_poe )
    rb_opts.pause_on_error = parse_rational_time( p + 1 );
  else
    rb_opts.pause_on_error = parse_time_interval( p );
  }


void parse_skipbs( const char * const ptr,
                   Rb_options & rb_opts,
                   const int hardbs )
  {
  const char * tail = ptr;

  if( tail[0] != ',' )
    rb_opts.skipbs = getnum( ptr, hardbs, 0, rb_opts.max_max_skipbs, &tail );
  if( tail[0] == ',' )
    rb_opts.max_skipbs = getnum( tail + 1, hardbs, Rb_options::min_skipbs,
                                 rb_opts.max_max_skipbs, &tail );
  if( tail[0] )
    {
    show_error( "Плохой разделитель в аргументе '--skip-size'", 0, true );
    std::exit( 1 );
    }
  if( rb_opts.skipbs > 0 && rb_opts.skipbs < Rb_options::min_skipbs )
    {
    show_error( "Минимальный начальный размер пропуска составляет 64KiB." );
    std::exit( 1 );
    }
  if( rb_opts.skipbs > rb_opts.max_skipbs )
    {
    show_error( "'initial skip size' больше чем 'max skip size'." );
    std::exit( 1 );
    }
  }


void check_o_direct()
  {
  if( O_DIRECT == 0 )
    { show_error( "Прямой доступ к диску недоступен." ); std::exit( 1 ); }
  }

} // end namespace


bool Rescuebook::reopen_infile()
  {
  if( ides_ >= 0 ) close( ides_ );
  // use same flags as do_rescue
  ides_ = open( iname_, O_RDONLY | o_direct_in | O_BINARY );
  if( ides_ < 0 )
    { final_msg( "Не удается открыть входной файл", errno ); return false; }
  const long long isize = lseek( ides_, 0, SEEK_END );
  if( isize < 0 )
    { final_msg( "Входной файл не доступен для поиска", errno ); return false; }
  return true;
  }

int main( const int argc, const char * const argv[] )
  {
  long long ipos = 0;
  long long opos = -1;
  long long max_size = -1;
  const char * domain_mapfile_name = 0;
  const char * test_mode_mapfile_name = 0;
  const int cluster_bytes = 65536;
  const int default_hardbs = 512;
  const int max_hardbs = 1 << 28;
  int cluster = 0;
  int hardbs = default_hardbs;
  int o_direct_out = 0;			// O_DIRECT or 0
  int o_trunc = 0;			// O_TRUNC or 0
  Mode program_mode = m_none;
  Fb_options fb_opts;
  Mb_options mb_opts;
  Rb_options rb_opts;
  bool ask = false;
  bool force = false;
  bool loose = false;
  bool preallocate = false;
  bool synchronous = false;
  bool verify_input_size = false;

  invocation_name = argv[0];
  command_line = argv[0];

  for( int i = 1; i < argc; ++i )
    {
      command_line += ' ';
      command_line += argv[i];
    }

  enum { opt_ask = 256, opt_cpa, opt_ds, opt_eoe, opt_eve, opt_mi,
         opt_msr, opt_poe, opt_pop, opt_rat, opt_rea, opt_rs, opt_sf };

  const Arg_parser::Option options[] =
    {
    { 'a', "min-read-rate",        Arg_parser::yes },
    { 'A', "try-again",            Arg_parser::no  },
    { 'b', "sector-size",          Arg_parser::yes },
    { 'B', "binary-prefixes",      Arg_parser::no  },
    { 'c', "cluster-size",         Arg_parser::yes },
    { 'C', "complete-only",        Arg_parser::no  },
    { 'd', "direct",               Arg_parser::no  },
    { 'd', "idirect",              Arg_parser::no  },
    { 'D', "odirect",              Arg_parser::no  },
    { 'e', "max-bad-areas",        Arg_parser::yes },
    { 'e', "max-errors",           Arg_parser::yes },
    { 'E', "max-error-rate",       Arg_parser::yes },
    { 'f', "force",                Arg_parser::no  },
    { 'F', "fill-mode",            Arg_parser::yes },
    { 'G', "generate-mode",        Arg_parser::no  },
    { 'h', "help",                 Arg_parser::no  },
    { 'H', "test-mode",            Arg_parser::yes },
    { 'i', "input-position",       Arg_parser::yes },
    { 'I', "verify-input-size",    Arg_parser::no  },
    { 'J', "verify-on-error",      Arg_parser::no  },
    { 'K', "skip-size",            Arg_parser::yes },
    { 'L', "loose-domain",         Arg_parser::no  },
    { 'm', "domain-mapfile",       Arg_parser::yes },
    { 'm', "domain-logfile",       Arg_parser::yes },
    { 'M', "retrim",               Arg_parser::no  },
    { 'n', "no-scrape",            Arg_parser::no  },
    { 'N', "no-trim",              Arg_parser::no  },
    { 'o', "output-position",      Arg_parser::yes },
    { 'O', "reopen-on-error",      Arg_parser::no  },
    { 'p', "preallocate",          Arg_parser::no  },
    { 'P', "data-preview",         Arg_parser::maybe },
    { 'q', "quiet",                Arg_parser::no  },
    { 'r', "retry-passes",         Arg_parser::yes },
    { 'R', "reverse",              Arg_parser::no  },
    { 's', "size",                 Arg_parser::yes },
    { 'S', "sparse",               Arg_parser::no  },
    { 't', "truncate",             Arg_parser::no  },
    { 'T', "timeout",              Arg_parser::yes },
    { 'u', "unidirectional",       Arg_parser::no  },
    { 'v', "verbose",              Arg_parser::no  },
    { 'V', "version",              Arg_parser::no  },
    { 'w', "ignore-write-errors",  Arg_parser::no  },
    { 'x', "extend-outfile",       Arg_parser::yes },
    { 'X', "max-read-errors",      Arg_parser::yes },
    { 'y', "synchronous",          Arg_parser::no  },
    { 'Z', "max-read-rate",        Arg_parser::yes },
    { opt_ask, "ask",              Arg_parser::no  },
    { opt_cpa, "cpass",            Arg_parser::yes },
    { opt_ds,  "delay-slow",       Arg_parser::yes },
    { opt_eoe, "exit-on-error",    Arg_parser::no  },
    { opt_eve, "log-events",       Arg_parser::yes },
    { opt_mi,  "mapfile-interval", Arg_parser::yes },
    { opt_msr, "max-slow-reads",   Arg_parser::yes },
    { opt_poe, "pause-on-error",   Arg_parser::yes },
    { opt_pop, "pause-on-pass",    Arg_parser::yes },
    { opt_pop, "pause",            Arg_parser::yes },
    { opt_rat, "log-rates",        Arg_parser::yes },
    { opt_rea, "log-reads",        Arg_parser::yes },
    { opt_rs,  "reset-slow",       Arg_parser::no  },
    { opt_sf,  "same-file",        Arg_parser::no  },
    {  0 , 0,                      Arg_parser::no  } };

  const Arg_parser parser( argc, argv, options );

  if( parser.error().size() )				// bad option
    {
      show_error( parser.error().c_str(), 0, true );
      return 1;
    }

  int argind = 0;
  for( ; argind < parser.arguments(); ++argind )
    {
      const int code = parser.code( argind );
      if( !code ) break;	// нет больше опций

      const std::string & sarg = parser.argument( argind );
      const char * const arg = sarg.c_str();

      switch( code )
      {
      case 'a': rb_opts.min_read_rate = getnum( arg, hardbs, 0 );
                break;
      case 'A': rb_opts.try_again = true;
                break;
      case 'b': hardbs = getnum( arg, 0, 1, max_hardbs );
                break;
      case 'B': format_num( 0, 0, -1 ); 		// set binary prefixes
                break;
      case 'c': cluster = getnum( arg, 0, 1, INT_MAX );
                break;
      case 'C': rb_opts.complete_only = true;
                break;
      case 'd': rb_opts.o_direct_in = O_DIRECT;
                check_o_direct();
                break;
      case 'D': o_direct_out = O_DIRECT;
                check_o_direct();
                break;
      case 'e': rb_opts.new_bad_areas_only = ( arg[0] == '+' );
                rb_opts.max_bad_areas = getnum( arg, 0, 0, LONG_MAX );
                break;
      case 'E': rb_opts.max_error_rate = getnum( arg, hardbs, 0 );
                break;
      case 'f': force = true;
                break;
      case 'F': set_mode( program_mode, m_fill );
                fb_opts.filltypes = sarg;
                fb_opts.write_location_data = check_types( fb_opts.filltypes, "fill-mode", true );
                break;
      case 'G': set_mode( program_mode, m_generate );
                break;
      case 'h': show_help( cluster_bytes / default_hardbs, default_hardbs );
                return 0;
      case 'H': set_name( &test_mode_mapfile_name, arg, code );
                break;
      case 'i': ipos = getnum( arg, hardbs, 0 );
                break;
      case 'I': verify_input_size = true;
                break;
      case 'J': rb_opts.verify_on_error = true;
                break;
      case 'K': parse_skipbs( arg, rb_opts, hardbs );
                break;
      case 'L': loose = true;
                break;
      case 'm': set_name( &domain_mapfile_name, arg, code );
                break;
      case 'M': rb_opts.retrim = true;
                break;
      case 'n': rb_opts.noscrape = true;
                break;
      case 'N': rb_opts.notrim = true;
                break;
      case 'o': opos = getnum( arg, hardbs, 0 );
                break;
      case 'O': rb_opts.reopen_on_error = true;
                break;
      case 'p': preallocate = true;
                break;
      case 'P': rb_opts.preview_lines = arg[0] ? getnum( arg, 0, 1, 32 ) : 3;
                break;
      case 'q': verbosity = -1;
                break;
      case 'r': rb_opts.max_retries = getnum( arg, 0, -1, INT_MAX / 2 );
                break;
      case 'R': rb_opts.reverse = true;
                break;
      case 's': max_size = getnum( arg, hardbs, -1 );
                break;
      case 'S': rb_opts.sparse = true;
                break;
      case 't': o_trunc = O_TRUNC;
                break;
      case 'T': rb_opts.timeout = parse_time_interval( arg );
                break;
      case 'u': rb_opts.unidirectional = true;
                break;
      case 'v': if( verbosity < 4 ) ++verbosity;
                break;
      case 'V': show_version();
                return 0;
      case 'w': fb_opts.ignore_write_errors = true;
                break;
      case 'x': rb_opts.min_outfile_size = getnum( arg, hardbs, 1 );
                break;
      case 'X': rb_opts.max_read_errors = getnum( arg, 0, 0, LONG_MAX );
                break;
      case 'y': synchronous = true;
                break;
      case 'Z': rb_opts.max_read_rate = getnum( arg, hardbs, 1 );
                break;
      case opt_ask: ask = true;
                    break;
      case opt_cpa: parse_cpass( arg, rb_opts );
                    break;
      case opt_ds:  rb_opts.delay_slow = parse_time_interval( arg );
                    break;
      case opt_eoe: rb_opts.max_read_errors = 0;
                    break;
      case opt_eve: if( event_logger.set_filename( arg ) ) break;
                    show_error( "Файл журнала событий существует и не является обычным файлом." );
                    return 1;
      case opt_mi:  parse_mapfile_intervals( arg, mb_opts );
                    break;
      case opt_msr: rb_opts.max_slow_reads = getnum( arg, 0, 0, LONG_MAX );
                    break;
      case opt_poe: parse_pause_on_error( arg, rb_opts );
                    break;
      case opt_pop: rb_opts.pause_on_pass = parse_time_interval( arg );
                    break;
      case opt_rat: if( rate_logger.set_filename( arg ) ) break;
                    show_error( "Файл журнала ставок существует и не является обычным файлом." );
                    return 1;
      case opt_rea: if( read_logger.set_filename( arg ) ) break;
                    show_error( "Чтение файла журнала существует и не является обычным файлом." );
                    return 1;
      case opt_rs:  rb_opts.reset_slow = true;
                    break;
      case opt_sf:  rb_opts.same_file = true;
                    break;
      default : internal_error( "неперехваченный вариант." );
      }
    } // end process options

  if( opos < 0 ) opos = ipos;
  if( hardbs < 1 ) hardbs = default_hardbs;
  if( cluster >= INT_MAX / hardbs ) cluster = ( INT_MAX / hardbs ) - 1;
  if( cluster < 1 ) cluster = cluster_bytes / hardbs;
  if( cluster < 1 ) cluster = 1;

  const char *iname = 0, *oname = 0, *mapname = 0;
  if( argind < parser.arguments() ) iname = parser.argument( argind++ ).c_str();

  if( argind < parser.arguments() ) oname = parser.argument( argind++ ).c_str();

  if( argind < parser.arguments() ) mapname = parser.argument( argind++ ).c_str();

  if( argind < parser.arguments() )
    {
      show_error( "Слишком много файлов.", 0, true );
      return 1;
    }

  // end scan arguments

  if( !check_files( iname, oname, mapname, rb_opts, force, program_mode == m_generate, preallocate )) return 1;

  Domain domain( ipos, max_size, domain_mapfile_name, loose );

  switch( program_mode )
    {
    case m_fill:
      if( ask )
        {
          show_error( "Опция '--ask' несовместима с режимом заполнения", 0, true );
          return 1;
        }

      if( rb_opts.same_file )
        {
          show_error( "Опция --same-file несовместима с режимом заполнения", 0, true );
          return 1;
        }

      if( rb_opts != Rb_options() || test_mode_mapfile_name || verify_input_size || preallocate || o_trunc )
        show_error( "предупреждение: Опции -aACdeEHIJKlMnOpPrRStTuxX игнорируются в режиме заполнения." );

      return do_fill( opos - ipos,
                      domain,
                      fb_opts,
                      mb_opts,
                      iname,
                      oname,
                      mapname,
                      cluster,
                      hardbs,
                      o_direct_out,
                      synchronous );

    case m_generate:
      if( ask )
        {
          show_error( "Опция --ask  несовместима с режимом генерации", 0, true );
          return 1;
        }

      if( fb_opts != Fb_options() ||
          rb_opts != Rb_options() ||
          synchronous ||
          test_mode_mapfile_name ||
          verify_input_size ||
          preallocate ||
          o_direct_out ||
          o_trunc )
        show_error( "предупреждение: Опции -aACdDeEHIJKlMnOpPrRStTuwxXy игнорируются в режиме генерации" );

      return do_generate( opos - ipos,
                          domain,
                          mb_opts,
                          iname,
                          oname,
                          mapname,
                          cluster,
                          hardbs );

    case m_none:
      {
      if( fb_opts != Fb_options() )
        {
          show_error( "Опция -w несовместима с данным режимом.", 0, true );
          return 1;
        }

      const Domain test_domain( 0, -1, test_mode_mapfile_name, loose );

      return do_rescue( opos - ipos,
                        domain,
                        test_mode_mapfile_name ? &test_domain : 0,
                        mb_opts,
                        rb_opts,
                        iname,
                        oname,
                        mapname,
                        cluster,
                        hardbs,
                        o_direct_out,
                        o_trunc,
                        ask,
                        preallocate,
                        synchronous,
                        verify_input_size );
      }
    }
  }
