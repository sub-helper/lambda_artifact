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

public class sudf_stddev_class {
    private static String m_url = "jdbc:default:connection";
    private static String remove_trailingspace_regex = "\\s+$";

    private static HashMap<String, Double> cache = new HashMap<String, Double>();
    @Function
    public static Double sudf_stddev(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_stddev((Object[]) new Double[] { input1, input2, input3 });
    }


    @Function(onNullInput = OnNullInput.RETURNS_NULL)
    public static Double sudf_stddev_null(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_stddev((Object[]) new Double[] { input1, input2, input3 });
    }

    @Function(parallel = Parallel.SAFE)
    public static Double sudf_stddev_safe(Double input1, Double input2, Double input3) throws SQLException {
        return evaluate_stddev((Object[]) new Double[] { input1, input2, input3 });
    }

    private static Double evaluate_stddev(Object[] data) {
        double mean = 0.0;
        double variance = 0;

        for (int i = 0; i < data.length; i++) {
            double val = 0.0;
            if (data[i] != null & data[i] instanceof Double) {
                val = ((Double) data[i]).doubleValue();
                mean += val;
            } else {
                return null;
            }
        }
        mean /= data.length;
        for (int i = 0; i < data.length; i++) {
            variance += Math.pow(((Double) data[i]).doubleValue() - mean, 2);
        }
        variance /= data.length;

        // Standard Deviation
        double std = Math.sqrt(variance);
        return Double.valueOf(std);
    }

    @Function
    public static Double sudf_stddev_cache(Double input1, Double input2, Double input3) throws SQLException {
        if (input1 == null || input2 == null || input3 == null) {
            return null;
        }
        String key = input1.toString() + input2.toString() + input3.toString();
        if (cache.containsKey(key)) {
            return cache.get(key);
        } else {
            Double result = evaluate_stddev((Object[]) new Double[] { input1, input2, input3 });
            cache.put(key, result);
            return result;
        }
    }
    

}