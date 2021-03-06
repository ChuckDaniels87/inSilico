//------------------------------------------------------------------------------
// <preamble>
// </preamble>
//------------------------------------------------------------------------------

//! @file   Format.hpp
//! @author Thomas Rueberg
//! @date   2012

#ifndef base_io_format_hpp
#define base_io_format_hpp

//------------------------------------------------------------------------------
// std includes
#include <iomanip>
#include <locale>
#include <string>
#include <sstream>
// boost includes
#include <boost/utility.hpp>
#include <boost/array.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
// base includes
#include <base/verify.hpp>

//------------------------------------------------------------------------------
namespace base{
    namespace io{

        //----------------------------------------------------------------------
        /** Return the basename of filename of type basename.suffix
         *  Calling
         *  \code{.cpp}
         *  const std::string baseName = base::io::baseName("file.smf", ".smf");
         *  \endcode
         *  will assign \a file to the variable \a baseName
         *
         */
        inline
        std::string baseName( const std::string& fileName,
                              const std::string& suffix )
        {
            return fileName.substr( 0, fileName.find( suffix ) );
        }

        //----------------------------------------------------------------------
        /** Create locale-formated string from value.
         *  In case of e.g. a US locale, you would get for a large number
         *  something like   xx,xxx,xxx.xxx .
         *  \tparam T        Type of value to convert to string
         *  \param[in] value Value to convert
         *  \return          Converted value in string format
         */
        template<typename T>
        std::string formatWithLocale(T value)
        {
            std::ostringstream oss;
            oss.imbue(std::locale(""));
            oss << std::fixed << value;
            return oss.str();
        }

        //----------------------------------------------------------------------
        /** Create string from given number and pad with leading zeros.
         *  \param[in] value  Value of the number to convert to a string
         *  \param[in] width  Field width determines the leading zeros
         *  \return           String with desired format
         */
        inline
        std::string leadingZeros( const unsigned value,
                                  const unsigned width = 4 )
        {
            std::ostringstream oss;
            oss << std::setw( static_cast<int>(width) )
                << std::setfill('0') << value;
            return oss.str();
        }

        //----------------------------------------------------------------------
        /** Convert argument to string.
         *  This function is just a wrapper for using boost's lexical-cast to
         *  a string. The motivation is a shorter writing, e.g.,
         *  \code{.cpp}
         *  VERIFY_MSG( number < 0, x2s("Bad value of ") + x2s( number ) );
         *  \endcode
         *  for quick writing. Note the using declaration
         *  \code{.cpp}
         *  using base::io::x2s;
         *  \endcode
         *  which reduces the function call to \a x2s without need of
         *  namespaces.
         *  \tparam T Type to be converted 
         *  \param[in] t Some object to be string-converted
         */
        template<typename T>
        std::string x2s( const T& t )
        {
            return boost::lexical_cast<std::string>( t );
        }

        //----------------------------------------------------------------------
        template<unsigned WIDTH> class Table;
 
    }
}

// This declaration reduces the expressions
using base::io::x2s;

//------------------------------------------------------------------------------
/** Create a table format for eye-candy output.
 *
 *  \code{.cpp}
 *  // Array of the widths of the table (example width=3)
 *  base::io::Table<3>::WidthArray widths = {{ W1, W2, W3 }};
 *  // Construct table oject
 *  base::io::Table<3> table( widths );
 *  // Fill table row with data
 *  table % Dat1;
 *  table % Dat2 % Dat3;
 *  // Table row is now filled, write the table
 *  std::cout << table;
 *  \endcode
 *  \tparam WIDTH  Number of entries in a table row
 */
template<unsigned WIDTH>
class base::io::Table
    : boost::noncopyable
{
public:
    //! Template parameter the number of entries
    static const unsigned width = WIDTH;

    //! Define self as type for convenience
    typedef Table<width>  SelfType;

    //! Array for the relative table positions
    typedef boost::array<unsigned,width> WidthArray;

    //! Constructor with field widths
    Table( const WidthArray&  widths ) : ctr_( 0 )
    {
        this -> setFormat_( widths, true);
    }

    //! Constructor for un-aligned tables
    Table() : ctr_( 0 )
    {
        WidthArray widths; widths.assign( 0 );
        this -> setFormat_( widths, false);
    }

private:
    //! Set the format object
    void setFormat_( const WidthArray& widths,
                     const bool withTabs )
    {
        std::string formatString;
                
        unsigned absolut = 0;
        for ( unsigned w = 0; w < width; w ++ ) {
            // absolut position in the line
            absolut += widths[w];
            // string with number of position
            const std::string widthString
                = "%|" + boost::lexical_cast<std::string>( absolut )
                + "t|";
            // string with number of argument
            const std::string numeral
                = boost::lexical_cast<std::string>( w+1 );
            // append to format string
            if ( withTabs ) formatString += widthString;
            else formatString += "   ";
            formatString +=  "%" + numeral + "%  ";
        }
        // add endline character
        formatString += "\n";
        // set format 
        formatter_.parse( formatString );
    }
    
public:
    //--------------------------------------------------------------------------
    //! Overload %-operator for easy entry feeding
    template<typename T> SelfType& operator%( const T& t )
    {
        // count number of entries
        ctr_++;
        // make sure that there not too many entries
        VERIFY_MSG( ctr_ <= width, "Cannot insert more fields to table" );
        // feed into format object
        formatter_ % t;
        // return self for concatenation of this operator
        return *this;
    }

    //--------------------------------------------------------------------------
    //! Write to given stream
    void flush( std::ostream& out ) 
    {
        // Add empty strings for not set fields
        for ( unsigned w = ctr_; w < width; w++ ) formatter_ % "";
        // Write self to stream
        out << formatter_;
        // Flush
        out << std::flush;
        
        ctr_ = 0;
    }

    //! For easier writing
    friend std::ostream& operator<<( std::ostream& out, SelfType& table )
    {
        table.flush( out );
        return out;
    }
            
private:
    boost::format formatter_; //!< Format object
    unsigned      ctr_;       //!< Counter of table row entries
};

#endif
