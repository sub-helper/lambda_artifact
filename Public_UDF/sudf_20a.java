package org.example.pgsql_udf;

import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import static org.postgresql.pljava.annotation.Function.Parallel;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.lang.annotation.RetentionPolicy;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;

public class sudf_20 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<Integer, String> cache = new HashMap<Integer, String>();
  private static HashMap<Integer, String> cache_complex = new HashMap<Integer, String>();

  public static String evaluation_complex(Integer id) throws SQLException {
    Integer result1 = null;
    Integer result2 = null;
    String final_result = "";
    String query1 =
      "select count(*) from store_sales_history, date_dim where ss_item_sk=? and d_date_sk=ss_sold_date_sk and d_year=2003";
    String query2 =
      "select count(*) from catalog_sales_history, date_dim where cs_item_sk=? and d_date_sk=cs_sold_date_sk and d_year=2003";

    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    stmt1.setInt(1, id);
    ResultSet rs1 = stmt1.executeQuery();
    if (rs1.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      result1 = rs1.getInt(1);
      if (result1 == null) {
        result1 = 0;
      }
    }
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt2.setInt(1, id);
    ResultSet rs2 = stmt2.executeQuery();
    if (rs2.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      result2 = rs2.getInt(1);
      if (result2 == null) {
        result2 = 0;
      }
    }
    stmt2.close();
    if (result1 > 0 && result2 > 0) {
      final_result = evaluation(id);
    } else {
      final_result = "outdated item";
    }
    conn.close();
    return final_result;
  }

  public static String evaluation(Integer id) throws SQLException {
    String result = null;
    String query = "select i_manufact from item where i_item_sk = ?";
    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt = conn.prepareStatement(query);
    stmt.setInt(1, id);
    ResultSet rs = stmt.executeQuery();
    if (rs.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      result = rs.getString("i_manufact");
      if (result != null) {
        result = result.replaceAll(remove_trailingspace_regex, "");
      }
    }
    stmt.close();
    conn.close();
    return result;
  }

  @Function
  public static String sudf_20a_GetManufactSimple_cache(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    if (cache.containsKey(id)) {
      return cache.get(id);
    } else {
      String result = evaluation(id);
      cache.put(id, result);
      return result;
    }
  }

  @Function
  public static String sudf_20a_GetManufactSimple(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation(id);
  }

  @Function
  public static String sudf_20b_GetManufactComplex(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation_complex(id);
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static String sudf_20a_GetManufactSimple_null(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation(id);
  }

  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static String sudf_20b_GetManufactComplex_null(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation_complex(id);
  }
  @Function(parallel = Parallel.SAFE)
  public static String sudf_20a_GetManufactSimple_safe(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation(id);
  }

  @Function(parallel = Parallel.SAFE)
  public static String sudf_20b_GetManufactComplex_safe(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    return evaluation_complex(id);
  }
  @Function
  public static String sudf_20b_GetManufactComplex_cache(Integer id)
    throws SQLException {
    if (id == null) {
      return null;
    }
    if (cache_complex.containsKey(id)) {
      return cache_complex.get(id);
    } else {
      String result = evaluation_complex(id);
      cache.put(id, result);
      return result;
    }
  }
}
