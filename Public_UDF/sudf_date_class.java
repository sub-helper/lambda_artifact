package org.example.pgsql_udf;

import java.text.DateFormat;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.Date;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.io.PrintStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import java.util.LinkedHashSet;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;
import java.util.SortedSet;
import java.util.TreeSet;
import java.lang.Object;
import java.lang.Double;
import java.time.*;
import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.Parallel;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import java.util.HashMap;

public class sudf_date_class {
    private static String m_url = "jdbc:default:connection";
    private static String remove_trailingspace_regex = "\\s+$";
    private static HashMap<String, LocalDate> nMonthsBefore_cache = new HashMap<String, LocalDate>();
    private static HashMap<String, LocalDate> dateFormat_cache = new HashMap<String, LocalDate>();

    
    
    
    @Function
    public static LocalDate sudf_nMonthsBefore(String date, Integer input) throws SQLException {
        if (input == null || date == null) {
            return null;
        }
        return evaluate_nMonthsBefore(date, input);
    }
    @Function(onNullInput = OnNullInput.RETURNS_NULL)
    public static LocalDate sudf_nMonthsBefore_null(String date, Integer input) throws SQLException {
        if (input == null || date == null) {
            return null;
        }
        return evaluate_nMonthsBefore(date, input);
    }
    @Function
    public static LocalDate sudf_nMonthsBefore_cache(String date, Integer input) throws SQLException {
        if (input == null || date == null) {
            return null;
        }
        String key = date.toString() + input.toString();
        if (nMonthsBefore_cache.containsKey(key)) {
            return nMonthsBefore_cache.get(key);
        } else {
            LocalDate result = evaluate_nMonthsBefore(date, input);
            nMonthsBefore_cache.put(key, result);
            return result;
        }
    }
    private static LocalDate evaluate_nMonthsBefore(String date, Integer input) throws SQLException {
        SimpleDateFormat secondFormatter = new SimpleDateFormat("yyyyMMdd");
        Date d = new Date();
        try {
            secondFormatter.setLenient(false);
            d = secondFormatter.parse(date);
            Calendar ca = Calendar.getInstance();
            ca.setTime(d);
            ca.add(Calendar.MONTH, -input);
            Date firstDate = ca.getTime();
            return firstDate.toInstant().atZone(ZoneId.systemDefault()).toLocalDate();
        } catch (ParseException ee) {
            return null;
        }
    }


    // properties: returns null on null
    private static LocalDate evaluate_dateformat(String date) {
        SimpleDateFormat firstFormatter = new SimpleDateFormat("yyyyMMdd");
        SimpleDateFormat secondFormatter = new SimpleDateFormat("yyyy-MM-dd");
        try {
            Date date1 = firstFormatter.parse(date);
            return date1.toInstant().atZone(ZoneId.systemDefault()).toLocalDate();
        } catch (Exception e) {
            Date d = new Date();
            try {
                secondFormatter.setLenient(false);
                d = secondFormatter.parse(date);
                return d.toInstant().atZone(ZoneId.systemDefault()).toLocalDate();
            } catch (ParseException ee) {
                try {
                    String originalDate = date;
                    String year = originalDate.substring(0, 4);
                    String month = originalDate.substring(4, 6);
                    String day = originalDate.substring(6, 8);
                    String date_now = year + "-" + month + "-" + day;
                    d = secondFormatter.parse(date_now);
                    return d.toInstant().atZone(ZoneId.systemDefault()).toLocalDate();
                } catch (Exception eee) {
                    return null;
                }
            }
        }
    }
    @Function
    public static LocalDate sudf_dateFormat(String date) throws SQLException {
        if (date == null) {
            return evaluate_dateformat("19000101");
        }
        return evaluate_dateformat(date);

    }
    @Function
    public static LocalDate sudf_dateFormat_cache(String date) throws SQLException {
        if (date == null) {
            return evaluate_dateformat("19000101");
        }
        String key = date;

        if (dateFormat_cache.containsKey(key)) {
            return dateFormat_cache.get(key);
        } else {
            LocalDate result = evaluate_dateformat(date);
            dateFormat_cache.put(key, result);
            return result;
        }
    }
    
}