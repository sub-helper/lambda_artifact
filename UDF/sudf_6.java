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

public class sudf_6 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<Integer, Double> cache = new HashMap<Integer, Double>();

  public static Double evaluation(Integer manufacture_id) throws SQLException {
    Double average = null;
    Double addition = null;
    String query1 =
      "select avg(ws_ext_discount_amt) from web_sales_history, item where ws_item_sk = i_item_sk and i_manufact_id = ?";
    String query2 =
      "select sum(ws_ext_discount_amt) from web_sales_history, item where ws_item_sk = i_item_sk and i_manufact_id = ? and ws_ext_discount_amt>1.3*?";

    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt1.setInt(1, manufacture_id);
    stmt2.setInt(1, manufacture_id);
    ResultSet rs1 = stmt1.executeQuery();
    if (rs1.next()) {
      average = rs1.getDouble(1);
      if (average == null) {
        average = Double.valueOf(0);
      }
    }
    stmt2.setDouble(2, average);
    ResultSet rs2 = stmt2.executeQuery();
    if (rs2.next()) {
      addition = rs2.getDouble(1);
      if (addition == null) {
        addition = Double.valueOf(0);
      }
    }

    stmt1.close();
    stmt2.close();

    conn.close();
    return addition;
  }

  @Function
  public static Double sudf_6_totalDiscount(Integer manufacture_id)
    throws SQLException {
    if (manufacture_id == null) {
      return null;
    }
    return evaluation(manufacture_id);
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static Double sudf_6_totalDiscount_null(Integer manufacture_id)
    throws SQLException {
    if (manufacture_id == null) {
      return null;
    }
    return evaluation(manufacture_id);
  }
  @Function(parallel = Parallel.SAFE)
  public static Double sudf_6_totalDiscount_safe(Integer manufacture_id)
    throws SQLException {
    if (manufacture_id == null) {
      return null;
    }
    return evaluation(manufacture_id);
  }

  @Function
  public static Double sudf_6_totalDiscount_cache(Integer manufacture_id)
    throws SQLException {
    if (manufacture_id == null) {
      return null;
    }
    if (cache.containsKey(manufacture_id)) {
      return cache.get(manufacture_id);
    } else {
      Double result = evaluation(manufacture_id);
      cache.put(manufacture_id, result);
      return result;
    }
  }
}
