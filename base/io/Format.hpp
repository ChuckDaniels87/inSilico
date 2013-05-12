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
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

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
        template<class T>
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
        std::string leadingZeros( const unsigned value,
                                  const unsigned width = 4 )
        {
            std::ostringstream oss;
            oss << std::setw(width) << std::setfill('0') << value;
            return oss.str();
        }

        //----------------------------------------------------------------------
        template<unsigned WIDTH> class Table;
 
    }
}



//------------------------------------------------------------------------------
/** Create a table format for eye-candy output.
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
        for ( unsigned w = ctr_; w < width; w++ )
            formatter_ % "";
        out << formatter_;
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
